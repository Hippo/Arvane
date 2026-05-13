#include <arvane/plugin/api.h>

extern "C" ARV_PLUGIN_EXPORT bool arvane_plugin_main(const ArvPluginAPI* api) {
  if (api == nullptr || api->api_version != ARV_PLUGIN_API_VERSION) {
    return false;
  }
  ArvPluginServiceRef service{};
  const ArvPluginResult result = api->services->get(
      api->services_ctx,
      arv_cstr("service_provider:example"),
      1,
      1,
      &service);
  if (result.code == ARV_PLUGIN_RESULT_OK && service.valid) {
    api->logger->info(api->logger_ctx, arv_cstr("Found example service."));
  }
  return true;
}
