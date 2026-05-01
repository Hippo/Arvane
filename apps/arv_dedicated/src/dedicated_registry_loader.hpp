#pragma once

#include "dedicated_error.hpp"

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/player/item.hpp"
#include "arv/voxel/block.hpp"

namespace arv::dedicated {

struct DedicatedRegistries {
  voxel::BlockRegistry block_registry;
  u64 block_registry_hash{};

  player::ItemRegistry item_registry;
  u64 item_registry_hash{};

  entity::EntityRegistry entity_registry;
};

[[nodiscard]] Expected<DedicatedRegistries, DedicatedError> load_dedicated_registries(
    const io::EngineConfig& config);

} // namespace arv::dedicated
