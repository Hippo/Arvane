#pragma once

#include "arv/api.h"
#include "arv/plugin/plugin_info.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace arv::asset {
class PackManager;
}

namespace arv::player {
class GameModeRegistry;
class ItemBehaviorRegistry;
}

namespace arv::voxel {
class BlockBehaviorRegistry;
}

namespace arv::world {
class EntityGoalRegistry;
}

namespace arv::plugin {

inline constexpr u32 PluginApiVersion = ARV_API_VERSION;

struct NativePluginPolicy {
  bool enabled{};
  std::vector<std::string> allowed_packs{};

  [[nodiscard]] bool allows(std::string_view pack_id) const noexcept;
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

[[nodiscard]] std::string current_native_platform_key();
[[nodiscard]] bool native_library_extension_matches_platform(
    const std::filesystem::path& path);

} // namespace arv::plugin
