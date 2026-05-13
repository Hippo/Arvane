#pragma once

#include "arv/api.h"

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

#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::plugin {

inline constexpr u32 PluginApiVersion = ARV_API_VERSION;

struct PluginError {
  std::string pack_id{};
  std::string phase{};
  std::filesystem::path file{};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

using PluginResult = Expected<void, PluginError>;

struct NativePluginPolicy {
  bool enabled{};
  std::vector<std::string> allowed_packs{};

  [[nodiscard]] bool allows(std::string_view pack_id) const noexcept;
};

enum class PluginState : u8 {
  Discovered,
  SkippedByPolicy,
  Loaded,
  Initialized,
  Failed,
  ShuttingDown,
  Shutdown,
};

struct PluginInfo {
  std::string pack_id{};
  std::string pack_version{};
  std::vector<std::string> namespaces{};
  asset::PackSide side{asset::PackSide::Both};
  PluginState state{PluginState::Discovered};
  std::filesystem::path library_path{};
  std::string entrypoint{};
  std::vector<std::string> diagnostics{};
};

struct PluginLoadOptions {
  const asset::PackManager* packs{};
  NativePluginPolicy policy{};
  std::filesystem::path config_root{};
  player::GameModeRegistry* game_modes{};
  voxel::BlockBehaviorRegistry* block_behaviors{};
  player::ItemBehaviorRegistry* item_behaviors{};
  world::EntityGoalRegistry* entity_goals{};
};

enum class PluginNetworkTargetKind : u8 {
  Player,
  Players,
  Broadcast,
  World,
};

struct PluginNetworkPacket {
  protocol::MessageType message_type{protocol::MessageType::Ping};
  protocol::ReliabilityClass reliability{protocol::ReliabilityClass::ReliableOrdered};
  protocol::NetworkLane lane{protocol::NetworkLane::Session};
  std::vector<u8> payload{};
};

struct PluginNetworkSendError {
  ArvResultCode code{ARV_RESULT_INTERNAL_ERROR};
  std::string message{};
};

struct PluginNetworkSendRequest {
  PluginNetworkTargetKind target_kind{PluginNetworkTargetKind::Player};
  PlayerID player{};
  std::span<const PlayerID> players{};
  WorldID world{};
  const PluginNetworkPacket* packet{};
};

using PluginNetworkSendResult = Expected<void, PluginNetworkSendError>;
using PluginNetworkSendFn =
    std::function<PluginNetworkSendResult(const PluginNetworkSendRequest&)>;
using PluginPlayerStableIDFn =
    std::function<std::optional<std::string>(PlayerID)>;

enum class PluginExtensionRegistryKind : u8 {
  EntityBehavior,
  ComponentCodec,
  LootFunction,
  RecipeType,
  WorldgenFeature,
  WorldgenNoise,
  WorldgenDensityFunction,
};

struct PluginRuntimeContext {
  world::WorldRuntime* default_world{};
  const voxel::BlockRegistry* block_registry{};
  const player::ItemRegistry* item_registry{};
  const entity::EntityRegistry* entity_registry{};
  player::GameModeRegistry* game_modes{};
  player::PermissionGroupRegistry* permission_groups{};
  sched::JobSystem* job_system{};
  TickIndex tick_index{};
  u64 world_seed{};
  std::filesystem::path server_save_root{};
  std::string world_name{};
  PluginPlayerStableIDFn player_stable_id{};
  PluginNetworkSendFn network_send{};
};

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

[[nodiscard]] const char* plugin_state_name(PluginState state) noexcept;
[[nodiscard]] std::string current_native_platform_key();
[[nodiscard]] bool native_library_extension_matches_platform(
    const std::filesystem::path& path);

} // namespace arv::plugin
