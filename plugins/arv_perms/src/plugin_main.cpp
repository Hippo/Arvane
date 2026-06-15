#include "arv_perms/arv_perms_plugin.hpp"

#include <memory>

namespace {

std::unique_ptr<arv_perms::ArvPermsPlugin> g_plugin;

} // namespace

ARV_MAIN(plugin) {
  g_plugin = std::make_unique<arv_perms::ArvPermsPlugin>(plugin);
  return g_plugin->initialize();
}
