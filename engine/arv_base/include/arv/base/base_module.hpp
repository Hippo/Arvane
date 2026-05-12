#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/permissions.hpp"
#include "arv/voxel/fluid.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace arv::base {

struct StarterInventoryItem {
  std::string_view item_name{};
  u16 count{};
};

struct BaseModuleRegistry {
  player::GameModeRegistry* game_modes{};
  entity::EntityBehaviorRegistry* entity_behaviors{};
  std::vector<entity::EntityDefinition>* entity_definitions{};
};

void register_base_module(BaseModuleRegistry& registry);

void register_base_game_modes(player::GameModeRegistry& registry);
void register_base_entity_behaviors(entity::EntityBehaviorRegistry& registry);

[[nodiscard]] std::vector<entity::EntityDefinition> base_entity_definitions();
[[nodiscard]] entity::EntityRegistry::Result build_base_entity_registry(
    std::vector<entity::EntityDefinition> pack_definitions);

[[nodiscard]] voxel::FluidDefinition make_base_water_fluid_definition(
    voxel::FluidTypeID type = voxel::DefaultWaterFluidTypeID) noexcept;

[[nodiscard]] std::span<const StarterInventoryItem> starter_inventory_items() noexcept;

} // namespace arv::base
