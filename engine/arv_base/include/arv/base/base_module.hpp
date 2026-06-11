#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/permissions.hpp"
#include "arv/voxel/fluid.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace arv::render {
class FluidRenderData;
}

namespace arv::voxel {
class BlockBehaviorRegistry;
class BlockRegistry;
}

namespace arv::player {
class ItemBehaviorRegistry;
}

namespace arv::world {
class EntityGoalRegistry;
}

namespace arv::server {
class BlockBreakBehaviorRegistry;
}

namespace arv::base {

struct StarterInventoryItem {
  std::string_view item_name{};
  u16 count{};
};

struct BaseModuleRegistry {
  player::GameModeRegistry* game_modes{};
  voxel::BlockBehaviorRegistry* block_behaviors{};
  player::ItemBehaviorRegistry* item_behaviors{};
  world::EntityGoalRegistry* entity_goals{};
  std::vector<entity::EntityDefinition>* entity_definitions{};
};

void register_base_module(BaseModuleRegistry& registry);

void register_base_game_modes(player::GameModeRegistry& registry);
void register_base_block_behaviors(voxel::BlockBehaviorRegistry& registry);
void register_base_item_behaviors(player::ItemBehaviorRegistry& registry);
void register_base_entity_goals(world::EntityGoalRegistry& registry);
void register_base_block_break_behaviors(server::BlockBreakBehaviorRegistry& registry);

[[nodiscard]] std::vector<entity::EntityDefinition> base_entity_definitions();
[[nodiscard]] entity::EntityRegistry::Result build_base_entity_registry(
    std::vector<entity::EntityDefinition> pack_definitions,
    const world::EntityGoalRegistry* goal_registry = nullptr,
    bool validate_goal_ids = true);

[[nodiscard]] voxel::FluidDefinition make_base_water_fluid_definition(
    voxel::FluidTypeID type = voxel::DefaultWaterFluidTypeID) noexcept;
[[nodiscard]] Expected<void, std::string> register_base_fluid_render_data(
    render::FluidRenderData& render_data,
    const voxel::BlockRegistry& block_registry);

[[nodiscard]] std::span<const StarterInventoryItem> starter_inventory_items() noexcept;

} // namespace arv::base
