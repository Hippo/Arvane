#include "plugin_manager_internal.hpp"

namespace arv::plugin {

std::vector<PluginInfo> PluginManager::plugins() const {
  std::vector<PluginInfo> infos;
  infos.reserve(impl_->plugins.size());
  for (const PluginRecord& plugin : impl_->plugins) {
    infos.push_back(plugin.info);
  }
  return infos;
}

std::span<const PluginError> PluginManager::diagnostics() const noexcept {
  return impl_->diagnostics;
}

usize PluginManager::loaded_plugin_count() const noexcept {
  usize count = 0;
  for (const PluginRecord& plugin : impl_->plugins) {
    if (plugin.info.state == PluginState::Initialized) {
      ++count;
    }
  }
  return count;
}

usize PluginManager::command_executor_count() const noexcept {
  return impl_->commands.size();
}

usize PluginManager::service_count() const noexcept {
  return static_cast<usize>(std::count_if(
      impl_->services.begin(),
      impl_->services.end(),
      [](const ServiceRegistration& service) {
        return service.active;
      }));
}

bool PluginManager::has_command_executor(std::string_view id) const noexcept {
  return std::any_of(
      impl_->commands.begin(),
      impl_->commands.end(),
      [&](const PluginCommandRegistration& command) {
        return command.executor_id == id;
      });
}

bool PluginManager::has_service(std::string_view id) const noexcept {
  return std::any_of(
      impl_->services.begin(),
      impl_->services.end(),
      [&](const ServiceRegistration& service) {
        return service.active && service.service_id == id;
      });
}

bool PluginManager::has_registered_extension(
    PluginExtensionRegistryKind kind,
    std::string_view id) const noexcept {
  return std::any_of(
      impl_->extension_registrations.begin(),
      impl_->extension_registrations.end(),
      [&](const PluginExtensionRegistration& registration) {
        return registration.kind == kind && registration.id == id;
      });
}

std::vector<std::string> PluginManager::registered_extension_ids(
    PluginExtensionRegistryKind kind) const {
  std::vector<std::string> ids;
  for (const PluginExtensionRegistration& registration :
       impl_->extension_registrations) {
    if (registration.kind == kind) {
      ids.push_back(registration.id);
    }
  }
  return ids;
}

} // namespace arv::plugin
