#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

ArvResult schedule_job(
    PluginCallContext* ctx,
    ScheduledJobKind kind,
    TickIndex delay,
    TickIndex interval,
    bool repeating,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (callback == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "scheduled job callback is null");
  }
  if (repeating && interval == 0U) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "repeating scheduled job interval must be non-zero");
  }
  if (ctx->impl->shutdown_started) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_STATE,
        "plugin manager is shutting down");
  }

  const u64 id = ctx->impl->next_job_id.fetch_add(1U, std::memory_order_relaxed);
  const TickIndex next_run =
      ctx->impl->runtime.tick_index + (delay == 0U ? 1U : delay);
  {
    std::lock_guard lock{ctx->impl->jobs_mutex};
    ctx->impl->jobs.push_back({
        id,
        ctx->plugin_index,
        kind,
        ScheduledJobTiming::Ticks,
        callback,
        user_data,
        next_run,
        interval,
        {},
        {},
        repeating,
        false,
        false,
    });
  }
  if (out_handle != nullptr) {
    out_handle->value = id;
  }
  return ok(ctx);
}

ArvResult schedule_millisecond_job(
    PluginCallContext* ctx,
    ScheduledJobKind kind,
    ArvMilliseconds delay_milliseconds,
    ArvMilliseconds interval_milliseconds,
    bool repeating,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (callback == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "scheduled job callback is null");
  }
  if (repeating && interval_milliseconds == 0U) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "repeating scheduled job interval must be non-zero");
  }
  if (ctx->impl->shutdown_started) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_STATE,
        "plugin manager is shutting down");
  }

  const auto now = std::chrono::steady_clock::now();
  const auto delay = std::chrono::milliseconds{delay_milliseconds};
  const auto interval = std::chrono::milliseconds{interval_milliseconds};
  const u64 id = ctx->impl->next_job_id.fetch_add(1U, std::memory_order_relaxed);
  {
    std::lock_guard lock{ctx->impl->jobs_mutex};
    ctx->impl->jobs.push_back({
        id,
        ctx->plugin_index,
        kind,
        ScheduledJobTiming::Milliseconds,
        callback,
        user_data,
        0U,
        0U,
        now + delay,
        interval,
        repeating,
        false,
        false,
    });
  }
  if (out_handle != nullptr) {
    out_handle->value = id;
  }
  return ok(ctx);
}

ArvResult api_sync_now(
    void* context,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Sync,
      0U,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvResult api_sync_delayed(
    void* context,
    ArvMilliseconds delay_milliseconds,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_millisecond_job(
      call_context(context),
      ScheduledJobKind::Sync,
      delay_milliseconds,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvResult api_sync_repeating(
    void* context,
    ArvMilliseconds start_delay_milliseconds,
    ArvMilliseconds interval_milliseconds,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_millisecond_job(
      call_context(context),
      ScheduledJobKind::Sync,
      start_delay_milliseconds,
      interval_milliseconds,
      true,
      callback,
      user_data,
      out_handle);
}

ArvResult api_async_now(
    void* context,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Async,
      0U,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvResult api_async_delayed(
    void* context,
    ArvMilliseconds delay_milliseconds,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_millisecond_job(
      call_context(context),
      ScheduledJobKind::Async,
      delay_milliseconds,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvResult api_async_repeating(
    void* context,
    ArvMilliseconds start_delay_milliseconds,
    ArvMilliseconds interval_milliseconds,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_millisecond_job(
      call_context(context),
      ScheduledJobKind::Async,
      start_delay_milliseconds,
      interval_milliseconds,
      true,
      callback,
      user_data,
      out_handle);
}

ArvResult api_sync_delayed_tick(
    void* context,
    ArvTickIndex delay_ticks,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Sync,
      delay_ticks,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvResult api_sync_repeating_tick(
    void* context,
    ArvTickIndex start_delay_ticks,
    ArvTickIndex interval_ticks,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Sync,
      start_delay_ticks,
      interval_ticks,
      true,
      callback,
      user_data,
      out_handle);
}

ArvResult api_async_delayed_tick(
    void* context,
    ArvTickIndex delay_ticks,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Async,
      delay_ticks,
      0U,
      false,
      callback,
      user_data,
      out_handle);
}

ArvResult api_async_repeating_tick(
    void* context,
    ArvTickIndex start_delay_ticks,
    ArvTickIndex interval_ticks,
    ArvJobCallback callback,
    void* user_data,
    ArvScheduledJobHandle* out_handle) {
  return schedule_job(
      call_context(context),
      ScheduledJobKind::Async,
      start_delay_ticks,
      interval_ticks,
      true,
      callback,
      user_data,
      out_handle);
}

ArvResult api_cancel_job(void* context, ArvScheduledJobHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  std::lock_guard lock{ctx->impl->jobs_mutex};
  for (ScheduledJob& job : ctx->impl->jobs) {
    if (job.id == handle.value && job.owner_index == ctx->plugin_index) {
      job.canceled = true;
      return ok(ctx);
    }
  }
  return make_result(ctx, ARV_RESULT_NOT_FOUND, "scheduled job not found");
}

} // namespace

void fill_scheduler_api(PluginAPIStorage& storage) {
  storage.scheduler_api = {
      &api_sync_now,
      &api_sync_delayed,
      &api_sync_repeating,
      &api_async_now,
      &api_async_delayed,
      &api_async_repeating,
      &api_cancel_job,
      &api_sync_delayed_tick,
      &api_sync_repeating_tick,
      &api_async_delayed_tick,
      &api_async_repeating_tick,
  };
}

} // namespace arv::plugin
