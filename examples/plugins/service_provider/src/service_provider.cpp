#include <arvane/plugin/api.h>

namespace {

int provider_value = 7;

}

extern "C" ARV_PLUGIN_EXPORT bool arvane_plugin_main(const ArvPluginAPI* api) {
  if (api == nullptr || api->api_version != ARV_PLUGIN_API_VERSION) {
    return false;
  }
  ArvPluginServiceDescriptor descriptor{};
  descriptor.service_id = arv_cstr("service_provider:example");
  descriptor.api_version = 1;
  descriptor.threading = ARV_PLUGIN_SERVICE_ANY_THREAD;
  descriptor.display_name = arv_cstr("Example Service");
  descriptor.api_table = &provider_value;
  ArvServiceHandle handle{};
  return api->services->publish(api->services_ctx, &descriptor, &handle).code ==
         ARV_PLUGIN_RESULT_OK;
}
