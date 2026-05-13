#pragma once

#include "arvane/plugin/api.h"

namespace arv::plugin {

class Scheduler {
public:
  explicit constexpr Scheduler(const ArvPluginAPI* api) noexcept : api_(api) {
  }

  ArvPluginResult sync(
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    if (api_ == nullptr || api_->scheduler == nullptr || api_->scheduler->sync_now == nullptr) {
      return {ARV_PLUGIN_RESULT_UNSUPPORTED, "scheduler API is unavailable"};
    }
    return api_->scheduler->sync_now(api_->scheduler_ctx, callback, user_data, out_handle);
  }

  ArvPluginResult sync(
      ArvPluginJobCallback callback,
      void* user_data,
      ArvTickIndex delay,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    if (api_ == nullptr || api_->scheduler == nullptr || api_->scheduler->sync_delayed == nullptr) {
      return {ARV_PLUGIN_RESULT_UNSUPPORTED, "scheduler API is unavailable"};
    }
    return api_->scheduler->sync_delayed(api_->scheduler_ctx, delay, callback, user_data, out_handle);
  }

  ArvPluginResult sync_repeat(
      ArvPluginJobCallback callback,
      void* user_data,
      ArvTickIndex start_delay,
      ArvTickIndex interval,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    if (api_ == nullptr || api_->scheduler == nullptr || api_->scheduler->sync_repeating == nullptr) {
      return {ARV_PLUGIN_RESULT_UNSUPPORTED, "scheduler API is unavailable"};
    }
    return api_->scheduler->sync_repeating(
        api_->scheduler_ctx,
        start_delay,
        interval,
        callback,
        user_data,
        out_handle);
  }

  ArvPluginResult async(
      ArvPluginJobCallback callback,
      void* user_data,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    if (api_ == nullptr || api_->scheduler == nullptr || api_->scheduler->async_now == nullptr) {
      return {ARV_PLUGIN_RESULT_UNSUPPORTED, "scheduler API is unavailable"};
    }
    return api_->scheduler->async_now(api_->scheduler_ctx, callback, user_data, out_handle);
  }

  ArvPluginResult async(
      ArvPluginJobCallback callback,
      void* user_data,
      ArvTickIndex delay,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    if (api_ == nullptr || api_->scheduler == nullptr || api_->scheduler->async_delayed == nullptr) {
      return {ARV_PLUGIN_RESULT_UNSUPPORTED, "scheduler API is unavailable"};
    }
    return api_->scheduler->async_delayed(api_->scheduler_ctx, delay, callback, user_data, out_handle);
  }

  ArvPluginResult async_repeat(
      ArvPluginJobCallback callback,
      void* user_data,
      ArvTickIndex start_delay,
      ArvTickIndex interval,
      ArvScheduledJobHandle* out_handle = nullptr) const {
    if (api_ == nullptr || api_->scheduler == nullptr || api_->scheduler->async_repeating == nullptr) {
      return {ARV_PLUGIN_RESULT_UNSUPPORTED, "scheduler API is unavailable"};
    }
    return api_->scheduler->async_repeating(
        api_->scheduler_ctx,
        start_delay,
        interval,
        callback,
        user_data,
        out_handle);
  }

  ArvPluginResult cancel(ArvScheduledJobHandle handle) const {
    if (api_ == nullptr || api_->scheduler == nullptr || api_->scheduler->cancel == nullptr) {
      return {ARV_PLUGIN_RESULT_UNSUPPORTED, "scheduler API is unavailable"};
    }
    return api_->scheduler->cancel(api_->scheduler_ctx, handle);
  }

private:
  const ArvPluginAPI* api_{};
};

} // namespace arv::plugin
