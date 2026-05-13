#include <arvane/plugin/api.h>

extern "C" ARV_PLUGIN_EXPORT bool not_arvane_plugin_main(const ArvPluginAPI*) {
  return true;
}
