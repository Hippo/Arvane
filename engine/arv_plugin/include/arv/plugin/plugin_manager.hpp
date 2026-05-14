#pragma once

#include "arv/plugin/plugin_info.hpp"
#include "arv/plugin/plugin_loading.hpp"
#include "arv/plugin/plugin_network.hpp"
#include "arv/plugin/plugin_runtime.hpp"

#include "arv/asset/pack.hpp"
#include "arv/command/command_registry.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/item.hpp"
#include "arv/protocol/message_type.hpp"
#include "arv/sched/job_system.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/entity_goal_registry.hpp"
#include "arv/world/world_runtime.hpp"

#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::plugin {

struct PluginManagerImpl;

class PluginManager {
public:
  PluginManager();
  ~PluginManager();

  PluginManager(PluginManager&&) noexcept;
  PluginManager& operator=(PluginManager&&) noexcept;

  PluginManager(const PluginManager&) = delete;
  PluginManager& operator=(const PluginManager&) = delete;

  [[nodiscard]] PluginResult load_plugins(PluginLoadOptions options);
  void register_commands(command::CommandRegistry& registry);
  void attach_runtime(PluginRuntimeContext context);
  void stop_runtime();
  void tick(TickIndex tick_index);
  void dispatch_event(std::string_view event_id);
  void shutdown();

  [[nodiscard]] std::vector<PluginInfo> plugins() const;
  [[nodiscard]] std::span<const PluginError> diagnostics() const noexcept;
  [[nodiscard]] usize loaded_plugin_count() const noexcept;
  [[nodiscard]] usize command_executor_count() const noexcept;
  [[nodiscard]] usize service_count() const noexcept;
  [[nodiscard]] bool has_command_executor(std::string_view id) const noexcept;
  [[nodiscard]] bool has_service(std::string_view id) const noexcept;
  [[nodiscard]] bool has_registered_extension(
      PluginExtensionRegistryKind kind,
      std::string_view id) const noexcept;
  [[nodiscard]] std::vector<std::string> registered_extension_ids(
      PluginExtensionRegistryKind kind) const;

private:
  std::unique_ptr<PluginManagerImpl> impl_;
};

} // namespace arv::plugin
