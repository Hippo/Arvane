#include <arvane/plugin/api.h>

ARV_PLUGIN_EXPORT bool arvane_plugin_main(const ArvPluginAPI* api) {
  if (api == 0 || api->api_version != ARV_PLUGIN_API_VERSION) {
    return false;
  }
  api->logger->info(api->logger_ctx, arv_cstr("C plugin loaded."));
  return true;
}
