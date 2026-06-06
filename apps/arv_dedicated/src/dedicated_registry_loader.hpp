#pragma once

#include "dedicated_error.hpp"

#include "arv/asset/pack.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/player/item.hpp"
#include "arv/text/language.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/entity_goal_registry.hpp"

namespace arv::dedicated {

struct DedicatedRegistries {
  arv::text::LanguageCatalog language;

  voxel::BlockRegistry block_registry;
  u64 block_registry_hash{};

  player::ItemRegistry item_registry;
  u64 item_registry_hash{};

  entity::EntityRegistry entity_registry;
  entity::VehicleDefinitionRegistry vehicle_registry;
};

[[nodiscard]] Expected<DedicatedRegistries, DedicatedError> load_dedicated_registries(
    const io::EngineConfig& config,
    const asset::PackManager& packs,
    const voxel::BlockBehaviorRegistry& block_behaviors,
    const player::ItemBehaviorRegistry& item_behaviors,
    const world::EntityGoalRegistry& entity_goals);

} // namespace arv::dedicated
