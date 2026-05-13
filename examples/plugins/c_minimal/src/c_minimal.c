#include <arv/api.h>

ARV_EXPORT bool arvane_plugin_main(const ArvAPI* api) {
  if (api == 0 || api->api_version != ARV_API_VERSION) {
    return false;
  }
  api->logger->info(api->logger_ctx, arv_cstr("C plugin loaded."));
  return true;
}
