#include "plugin_manager_internal.hpp"

namespace arv::plugin {

void PluginManager::attach_runtime(PluginRuntimeContext context) {
  const player::PermissionGroupRegistry* previous_group_registry =
      impl_->runtime.permission_groups;
  const world::WorldRuntime* previous_world = impl_->runtime.default_world;
  if (previous_group_registry != context.permission_groups) {
    for (PermissionGroupRegistration& registration : impl_->permission_groups) {
      registration.installed = false;
    }
  }
  if (previous_world != context.default_world) {
    impl_->persistent_regions_installed = false;
  }
  impl_->runtime = context;
  install_plugin_permission_groups(*impl_);
  install_persistent_regions(*impl_);
  if (!impl_->runtime_active && context.default_world != nullptr) {
    impl_->runtime_active = true;
    impl_->shutdown_event_dispatched = false;
    dispatch_event("arvane:event/server_start");
  }
}

void PluginManager::stop_runtime() {
  if (!impl_->runtime_active) {
    impl_->runtime = {};
    return;
  }
  if (!impl_->shutdown_event_dispatched) {
    dispatch_event("arvane:event/server_stop");
    impl_->shutdown_event_dispatched = true;
  }
  {
    std::lock_guard lock{impl_->jobs_mutex};
    for (ScheduledJob& job : impl_->jobs) {
      job.canceled = true;
    }
    impl_->jobs.erase(
        std::remove_if(
            impl_->jobs.begin(),
            impl_->jobs.end(),
            [](const ScheduledJob& job) {
              return job.canceled && !job.running;
            }),
        impl_->jobs.end());
  }
  if (impl_->runtime.default_world != nullptr) {
    for (RuntimeRegionRegistration& registration : impl_->runtime_regions) {
      if (registration.active) {
        (void)impl_->runtime.default_world->regions().remove_region(registration.id);
        registration.active = false;
      }
    }
    for (RuntimeZoneRegistration& registration : impl_->runtime_zones) {
      if (registration.active) {
        (void)impl_->runtime.default_world->regions().remove_zone(registration.id);
        registration.active = false;
      }
    }
  }
  impl_->runtime = {};
  impl_->runtime_active = false;
  impl_->persistent_regions_installed = false;
}

void PluginManager::tick(TickIndex tick_index) {
  impl_->runtime.tick_index = tick_index;
  if (impl_->shutdown_started) {
    return;
  }

  struct JobToRun {
    u64 id{};
    usize owner_index{};
    ScheduledJobKind kind{};
    ArvJobCallback callback{};
    void* user_data{};
  };
  std::vector<JobToRun> due;
  const auto now = std::chrono::steady_clock::now();
  {
    std::lock_guard lock{impl_->jobs_mutex};
    for (ScheduledJob& job : impl_->jobs) {
      const bool job_due =
          job.timing == ScheduledJobTiming::Ticks
              ? job.next_run <= tick_index
              : job.next_time <= now;
      if (job.canceled || job.running || !job_due) {
        continue;
      }
      due.push_back({job.id, job.owner_index, job.kind, job.callback, job.user_data});
      if (job.kind == ScheduledJobKind::Async) {
        job.running = true;
      }
      if (job.repeating) {
        if (job.timing == ScheduledJobTiming::Ticks) {
          job.next_run = tick_index + job.interval;
        } else {
          job.next_time = now + job.interval_time;
        }
      } else {
        job.canceled = true;
      }
    }
  }

  for (const JobToRun& job : due) {
    if (job.owner_index >= impl_->plugins.size() ||
        impl_->plugins[job.owner_index].info.state != PluginState::Initialized) {
      continue;
    }
    if (job.kind == ScheduledJobKind::Sync) {
      ThreadContextScope sync_scope{PluginThreadContext::Sync};
      job.callback(job.user_data);
      continue;
    }
    if (impl_->runtime.job_system == nullptr) {
      append_diagnostic(
          *impl_,
          impl_->plugins[job.owner_index].info.pack_id,
          "scheduler",
          {},
          "async plugin job could not run because no JobSystem is attached");
      std::lock_guard lock{impl_->jobs_mutex};
      for (ScheduledJob& stored : impl_->jobs) {
        if (stored.id == job.id) {
          stored.running = false;
          stored.canceled = true;
        }
      }
      continue;
    }
    sched::JobDesc desc;
    desc.name = "plugin/" + impl_->plugins[job.owner_index].info.pack_id;
    desc.lane = sched::JobLane::BackgroundCpu;
    desc.domain = sched::JobDomain::Generic;
    sched::JobHandle handle = impl_->runtime.job_system->submit(
        std::move(desc),
        [impl = impl_.get(), job](const sched::CancellationToken& token) {
          if (!token.is_canceled()) {
            ThreadContextScope async_scope{PluginThreadContext::Async};
            job.callback(job.user_data);
          }
          std::lock_guard lock{impl->jobs_mutex};
          for (ScheduledJob& stored : impl->jobs) {
            if (stored.id == job.id) {
              stored.running = false;
              break;
            }
          }
        });
    if (!handle.valid()) {
      append_diagnostic(
          *impl_,
          impl_->plugins[job.owner_index].info.pack_id,
          "scheduler",
          {},
          "async plugin job was not accepted by JobSystem");
      std::lock_guard lock{impl_->jobs_mutex};
      for (ScheduledJob& stored : impl_->jobs) {
        if (stored.id == job.id) {
          stored.running = false;
          stored.canceled = true;
        }
      }
    }
  }

  std::lock_guard lock{impl_->jobs_mutex};
  impl_->jobs.erase(
      std::remove_if(
          impl_->jobs.begin(),
          impl_->jobs.end(),
          [](const ScheduledJob& job) {
            return job.canceled && !job.running;
          }),
      impl_->jobs.end());
}

void PluginManager::dispatch_event(std::string_view event_id) {
  std::vector<const EventSubscription*> listeners;
  for (const EventSubscription& subscription : impl_->subscriptions) {
    if (!subscription.active || subscription.event_id != event_id) {
      continue;
    }
    listeners.push_back(&subscription);
  }
  std::sort(listeners.begin(), listeners.end(), [&](const EventSubscription* lhs, const EventSubscription* rhs) {
    if (lhs->priority != rhs->priority) {
      return static_cast<int>(lhs->priority) > static_cast<int>(rhs->priority);
    }
    const usize lhs_order = impl_->plugins[lhs->owner_index].pack_order;
    const usize rhs_order = impl_->plugins[rhs->owner_index].pack_order;
    if (lhs_order != rhs_order) {
      return lhs_order < rhs_order;
    }
    return lhs->event_id < rhs->event_id;
  });

  ArvEvent event;
  event.tick = impl_->runtime.tick_index;
  event.world = impl_->runtime.default_world == nullptr
                    ? 0U
                    : impl_->runtime.default_world->id().value;
  event.payload = {};
  ThreadContextScope sync_scope{PluginThreadContext::Sync};
  const std::string stable_event_id{event_id};
  event.event_id = stable_view(stable_event_id);
  for (const EventSubscription* subscription : listeners) {
    if (subscription->owner_index >= impl_->plugins.size() ||
        impl_->plugins[subscription->owner_index].info.state != PluginState::Initialized) {
      continue;
    }
    subscription->callback(&event, subscription->user_data);
  }
}

void PluginManager::shutdown() {
  if (!impl_ || impl_->shutdown_started) {
    return;
  }
  impl_->shutdown_started = true;
  stop_runtime();
  {
    std::lock_guard lock{impl_->jobs_mutex};
    for (ScheduledJob& job : impl_->jobs) {
      job.canceled = true;
    }
  }
  for (ServiceRegistration& service : impl_->services) {
    service.active = false;
  }
  for (EventSubscription& subscription : impl_->subscriptions) {
    subscription.active = false;
  }
  for (PluginRecord& plugin : impl_->plugins) {
    if (plugin.info.state == PluginState::Initialized ||
        plugin.info.state == PluginState::Loaded) {
      plugin.info.state = PluginState::Shutdown;
    }
  }
}


} // namespace arv::plugin
