#include "plugin_manager_internal.hpp"

namespace arv::plugin {
namespace {

ArvResult api_publish_service(
    void* context,
    const ArvServiceDescriptor* descriptor,
    ArvServiceHandle* out_handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (descriptor == nullptr || descriptor->api_table == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "service descriptor or API table is null");
  }
  if (descriptor->api_version == 0U) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "service API version must be non-zero");
  }
  const std::string id{abi_view(descriptor->service_id)};
  if (auto error = validate_owned_resource_id(ctx->info(), id);
      error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  const u64 handle = ctx->impl->next_service_id++;
  ctx->impl->services.push_back({
      handle,
      ctx->plugin_index,
      id,
      descriptor->api_version,
      descriptor->priority,
      descriptor->threading,
      abi_string(descriptor->display_name),
      descriptor->api_table,
      descriptor->user_data,
      true,
  });
  if (out_handle != nullptr) {
    out_handle->value = handle;
  }
  return ok(ctx);
}

ArvResult api_unpublish_service(void* context, ArvServiceHandle handle) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  for (ServiceRegistration& service : ctx->impl->services) {
    if (service.id == handle.value &&
        service.owner_index == ctx->plugin_index &&
        service.active) {
      service.active = false;
      return ok(ctx);
    }
  }
  return make_result(ctx, ARV_RESULT_NOT_FOUND, "service not found");
}

[[nodiscard]] bool service_version_matches(
    const ServiceRegistration& service,
    u32 min_version,
    u32 max_version) noexcept {
  const bool above_min = min_version == 0U || service.api_version >= min_version;
  const bool below_max = max_version == 0U || service.api_version <= max_version;
  return above_min && below_max;
}

[[nodiscard]] std::vector<const ServiceRegistration*> matching_services(
    PluginManagerImpl& impl,
    std::string_view service_id,
    u32 min_api_version,
    u32 max_api_version) {
  std::vector<const ServiceRegistration*> matches;
  for (const ServiceRegistration& service : impl.services) {
    if (!service.active || service.service_id != service_id ||
        !service_version_matches(service, min_api_version, max_api_version)) {
      continue;
    }
    matches.push_back(&service);
  }
  std::sort(matches.begin(), matches.end(), [&](const ServiceRegistration* lhs, const ServiceRegistration* rhs) {
    if (lhs->priority != rhs->priority) {
      return lhs->priority > rhs->priority;
    }
    const usize lhs_order = impl.plugins[lhs->owner_index].pack_order;
    const usize rhs_order = impl.plugins[rhs->owner_index].pack_order;
    if (lhs_order != rhs_order) {
      return lhs_order < rhs_order;
    }
    return lhs->service_id < rhs->service_id;
  });
  return matches;
}

[[nodiscard]] ArvServiceRef service_ref_for(
    const PluginManagerImpl& impl,
    const ServiceRegistration& service) {
  return {
      ArvServiceHandle{service.id},
      stable_view(service.service_id),
      service.api_version,
      stable_view(impl.plugins[service.owner_index].info.pack_id),
      service.api_table,
      service.user_data,
      service.active,
  };
}

ArvResult api_get_service(
    void* context,
    ArvResourceID service_id,
    uint32_t min_api_version,
    uint32_t max_api_version,
    ArvServiceRef* out_service) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return null_context_result(
        ARV_RESULT_INVALID_STATE,
        "plugin API context is null");
  }
  if (out_service == nullptr) {
    return make_result(
        ctx,
        ARV_RESULT_INVALID_ARGUMENT,
        "out_service is null");
  }
  const std::string id{abi_view(service_id)};
  if (auto error = validate_resource_id(id); error.has_value()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, error.value());
  }
  std::vector<const ServiceRegistration*> matches =
      matching_services(*ctx->impl, id, min_api_version, max_api_version);
  if (matches.empty()) {
    *out_service = {};
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "service not found");
  }
  *out_service = service_ref_for(*ctx->impl, *matches.front());
  return ok(ctx);
}

uint64_t api_list_services(
    void* context,
    ArvResourceID service_id,
    uint32_t min_api_version,
    uint32_t max_api_version,
    ArvServiceRef* out_services,
    uint64_t max_services) {
  PluginCallContext* ctx = call_context(context);
  if (ctx == nullptr) {
    return 0U;
  }
  const std::string id{abi_view(service_id)};
  if (validate_resource_id(id).has_value()) {
    return 0U;
  }
  std::vector<const ServiceRegistration*> matches =
      matching_services(*ctx->impl, id, min_api_version, max_api_version);
  if (out_services != nullptr && max_services > 0U) {
    const uint64_t count = std::min<uint64_t>(max_services, matches.size());
    for (uint64_t i = 0; i < count; ++i) {
      out_services[i] = service_ref_for(*ctx->impl, *matches[i]);
    }
  }
  return matches.size();
}

void api_release_service(void* context, ArvServiceRef service) {
  (void)context;
  (void)service;
}

} // namespace

void fill_service_api(PluginAPIStorage& storage) {
  storage.services_api = {
      &api_publish_service,
      &api_unpublish_service,
      &api_get_service,
      &api_list_services,
      &api_release_service,
  };
}

} // namespace arv::plugin
