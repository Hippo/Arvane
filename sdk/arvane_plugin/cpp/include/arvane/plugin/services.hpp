#pragma once

#include "arvane/plugin/api.h"
#include "arvane/plugin/logger.hpp"

#include <string_view>

namespace arv::plugin {

class Services {
public:
  explicit constexpr Services(const ArvPluginAPI* api) noexcept : api_(api) {
  }

  ArvPluginResult publish(
      std::string_view id,
      uint32_t api_version,
      const void* api_table,
      ArvServiceHandle* out_handle = nullptr,
      int32_t priority = 0,
      ArvPluginServiceThreading threading = ARV_PLUGIN_SERVICE_SYNC_ONLY,
      std::string_view display_name = {}) const {
    if (api_ == nullptr || api_->services == nullptr || api_->services->publish == nullptr) {
      return {ARV_PLUGIN_RESULT_UNSUPPORTED, "service API is unavailable"};
    }
    ArvPluginServiceDescriptor descriptor{};
    descriptor.service_id = view(id);
    descriptor.api_version = api_version;
    descriptor.priority = priority;
    descriptor.threading = threading;
    descriptor.display_name = view(display_name);
    descriptor.api_table = api_table;
    return api_->services->publish(api_->services_ctx, &descriptor, out_handle);
  }

  ArvPluginResult get(
      std::string_view id,
      uint32_t min_api_version,
      uint32_t max_api_version,
      ArvPluginServiceRef* out_service) const {
    if (api_ == nullptr || api_->services == nullptr || api_->services->get == nullptr) {
      return {ARV_PLUGIN_RESULT_UNSUPPORTED, "service API is unavailable"};
    }
    return api_->services->get(
        api_->services_ctx,
        view(id),
        min_api_version,
        max_api_version,
        out_service);
  }

private:
  const ArvPluginAPI* api_{};
};

} // namespace arv::plugin
