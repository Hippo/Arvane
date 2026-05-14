#include "plugin_manager_internal.hpp"

namespace arv::plugin {

PluginManager::PluginManager() : impl_(std::make_unique<PluginManagerImpl>()) {
}

PluginManager::~PluginManager() {
  shutdown();
}

PluginManager::PluginManager(PluginManager&&) noexcept = default;

PluginManager& PluginManager::operator=(PluginManager&&) noexcept = default;

} // namespace arv::plugin
