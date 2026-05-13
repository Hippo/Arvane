#include <arvane/plugin/plugin.hpp>

extern "C" ARV_PLUGIN_EXPORT bool arvane_plugin_main(const ArvPluginAPI* api) {
  if (api == nullptr || api->api_version != ARV_PLUGIN_API_VERSION) {
    return false;
  }
  arv::plugin::Plugin plugin{api};
  plugin.logger().info("C++ plugin loaded.");
  return true;
}
