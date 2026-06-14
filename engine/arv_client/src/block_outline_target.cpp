#include "arv/client/block_outline_target.hpp"

#include "arv/physics/raycast.hpp"

namespace arv::client {

std::optional<BlockOutlineTarget> find_block_outline_target(
    const physics::ICollisionQuery& query,
    const voxel::BlockRegistry& block_registry,
    Vec3f origin,
    Vec3f direction,
    f32 max_distance) {
  const std::optional<physics::BlockRaycastHit> hit =
      physics::raycast_blocks(query, origin, direction, max_distance);
  if (!hit.has_value() || block_registry.is_air(hit->variant)) {
    return std::nullopt;
  }
  return BlockOutlineTarget{hit->block, hit->variant};
}

} // namespace arv::client
