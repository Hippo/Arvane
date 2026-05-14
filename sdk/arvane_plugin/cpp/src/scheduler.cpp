#include "arv/scheduler.hpp"

namespace arv {

void Job::operator()() const {
  if (handler_) {
    handler_();
  }
}

ArvJobCallback Job::callback() const noexcept {
  return &invoke;
}

void* Job::user_data() noexcept {
  return this;
}

void Job::invoke(void* user_data) {
  auto* self = static_cast<Job*>(user_data);
  if (self != nullptr) {
    (*self)();
  }
}

Scheduler::Scheduler(const ArvAPI* api) noexcept : api_(api) {
}

Result Scheduler::sync_repeat(
    std::chrono::milliseconds start_delay,
    std::chrono::milliseconds interval,
    Job& job,
    ArvScheduledJobHandle* out_handle) const {
  if (api_ == nullptr || api_->scheduler == nullptr ||
      api_->scheduler->sync_repeating == nullptr) {
    return Result::unsupported("scheduler API is unavailable");
  }
  return Result{api_->scheduler->sync_repeating(
      api_->scheduler_ctx,
      milliseconds(start_delay),
      milliseconds(interval),
      job.callback(),
      job.user_data(),
      out_handle)};
}

Result Scheduler::sync_repeat_tick(
    ArvTickIndex start_delay,
    ArvTickIndex interval,
    Job& job,
    ArvScheduledJobHandle* out_handle) const {
  if (api_ == nullptr || api_->scheduler == nullptr ||
      api_->scheduler->sync_repeating_tick == nullptr) {
    return Result::unsupported("scheduler API is unavailable");
  }
  return Result{api_->scheduler->sync_repeating_tick(
      api_->scheduler_ctx,
      start_delay,
      interval,
      job.callback(),
      job.user_data(),
      out_handle)};
}

Result Scheduler::async_repeat(
    std::chrono::milliseconds start_delay,
    std::chrono::milliseconds interval,
    Job& job,
    ArvScheduledJobHandle* out_handle) const {
  if (api_ == nullptr || api_->scheduler == nullptr ||
      api_->scheduler->async_repeating == nullptr) {
    return Result::unsupported("scheduler API is unavailable");
  }
  return Result{api_->scheduler->async_repeating(
      api_->scheduler_ctx,
      milliseconds(start_delay),
      milliseconds(interval),
      job.callback(),
      job.user_data(),
      out_handle)};
}

Result Scheduler::async_repeat_tick(
    ArvTickIndex start_delay,
    ArvTickIndex interval,
    Job& job,
    ArvScheduledJobHandle* out_handle) const {
  if (api_ == nullptr || api_->scheduler == nullptr ||
      api_->scheduler->async_repeating_tick == nullptr) {
    return Result::unsupported("scheduler API is unavailable");
  }
  return Result{api_->scheduler->async_repeating_tick(
      api_->scheduler_ctx,
      start_delay,
      interval,
      job.callback(),
      job.user_data(),
      out_handle)};
}

Result Scheduler::cancel(ArvScheduledJobHandle handle) const {
  if (api_ == nullptr || api_->scheduler == nullptr || api_->scheduler->cancel == nullptr) {
    return Result::unsupported("scheduler API is unavailable");
  }
  return Result{api_->scheduler->cancel(api_->scheduler_ctx, handle)};
}

ArvMilliseconds Scheduler::milliseconds(std::chrono::milliseconds duration) noexcept {
  return duration.count() <= 0
             ? 0U
             : static_cast<ArvMilliseconds>(duration.count());
}

} // namespace arv
