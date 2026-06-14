#pragma once

#include "arv/physics/collision_query.hpp"
#include "arv/voxel/block.hpp"

#include <optional>

namespace arv::client {

struct BlockOutlineTarget {
  BlockPos block{};
  voxel::BlockVariantID variant{};
};

[[nodiscard]] std::optional<BlockOutlineTarget> find_block_outline_target(
    const physics::ICollisionQuery& query,
    const voxel::BlockRegistry& block_registry,
    Vec3f origin,
    Vec3f direction,
    f32 max_distance);

} // namespace arv::client
