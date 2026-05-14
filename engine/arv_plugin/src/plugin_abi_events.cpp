#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

ArvResult api_subscribe_event(
    void* context,
    ArvResourceID event_id,
    ArvEventPriority priority,
    ArvEventCallback callback,
    void* user_data,
    ArvEventSubscriptionHandle* out_handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (callback == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "event callback is null");
  }
  const std::string id{abi_view(event_id)};
  if (auto error = validate_resource_id(id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  const u64 handle = ctx->impl->next_subscription_id++;
  ctx->impl->subscriptions.push_back({
      handle,
      ctx->plugin_index,
      id,
      priority,
      callback,
      user_data,
      true,
  });
  if (out_handle != nullptr) {
    out_handle->value = handle;
  }
  return ok(ctx);
}

ArvResult api_unsubscribe_event(
    void* context,
    ArvEventSubscriptionHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  for (EventSubscription& subscription : ctx->impl->subscriptions) {
    if (subscription.id == handle.value &&
        subscription.owner_index == ctx->plugin_index &&
        subscription.active) {
      subscription.active = false;
      return ok(ctx);
    }
  }
  return make_result(ctx, ARV_RESULT_NOT_FOUND, "event subscription not found");
}

} // namespace

void fill_event_api(PluginAPIStorage& storage) {
  storage.events_api = {&api_subscribe_event, &api_unsubscribe_event};
}

} // namespace arv::plugin
