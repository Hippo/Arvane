#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/plugin/plugin_network.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace arv::entity {
class EntityRegistry;
class VehicleDefinitionRegistry;
} // namespace arv::entity

namespace arv::player {
class GameModeRegistry;
class ItemRegistry;
class PermissionGroupRegistry;
} // namespace arv::player

namespace arv::sched {
class JobSystem;
}

namespace arv::voxel {
class BlockRegistry;
}

namespace arv::world {
class WorldRuntime;
}

namespace arv::text {
class TextVariableStore;
}

namespace arv::plugin {

using PluginPlayerStableIDFn =
    std::function<std::optional<std::string>(PlayerID)>;
using PluginPlayerDisplayNameFn =
    std::function<std::optional<std::string>(PlayerID)>;
using PluginOnlinePlayersFn = std::function<std::vector<PlayerID>()>;

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
  const entity::VehicleDefinitionRegistry* vehicle_registry{};
  player::GameModeRegistry* game_modes{};
  player::PermissionGroupRegistry* permission_groups{};
  sched::JobSystem* job_system{};
  TickIndex tick_index{};
  u64 world_seed{};
  std::filesystem::path server_save_root{};
  std::filesystem::path config_storage_root{};
  bool config_storage_root_is_pack_root{};
  std::string world_name{};
  PluginPlayerStableIDFn player_stable_id{};
  PluginPlayerDisplayNameFn player_display_name{};
  PluginOnlinePlayersFn online_players{};
  PluginNetworkSendFn network_send{};
  text::TextVariableStore* text_variables{};
  PluginChatMessageSendFn chat_message_send{};
};

} // namespace arv::plugin
