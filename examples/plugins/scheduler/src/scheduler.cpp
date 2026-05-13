#include <arvane/plugin/api.h>

namespace {

void tick(void*) {
}

}

extern "C" ARV_PLUGIN_EXPORT bool arvane_plugin_main(const ArvPluginAPI* api) {
  if (api == nullptr || api->api_version != ARV_PLUGIN_API_VERSION) {
    return false;
  }
  ArvScheduledJobHandle handle{};
  return api->scheduler->sync_repeating(
             api->scheduler_ctx,
             1,
             20,
             &tick,
             nullptr,
             &handle)
             .code == ARV_PLUGIN_RESULT_OK;
}
