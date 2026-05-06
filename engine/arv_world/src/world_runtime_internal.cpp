#include "world_runtime_internal.hpp"

#include <cmath>

namespace arv::world::detail {

i32 abs_i32(i32 value) noexcept {
  return value < 0 ? -value : value;
}

i32 max_abs3(i32 x, i32 y, i32 z) noexcept {
  const i32 xy = abs_i32(x) > abs_i32(y) ? abs_i32(x) : abs_i32(y);
  return xy > abs_i32(z) ? xy : abs_i32(z);
}

BlockPos offset_block(BlockPos pos, Vec3i offset) noexcept {
  return {{pos.value.x + offset.x, pos.value.y + offset.y, pos.value.z + offset.z}};
}

ChunkSectionCoord chunk_for_position(Vec3f position) noexcept {
  return block_to_chunk({{
      static_cast<i32>(std::floor(position.x)),
      static_cast<i32>(std::floor(position.y)),
      static_cast<i32>(std::floor(position.z)),
  }});
}

SectionLocalPos local_from_index(usize index) noexcept {
  const i32 x = static_cast<i32>(index % voxel::ChunkSide);
  index /= voxel::ChunkSide;
  const i32 y = static_cast<i32>(index % voxel::ChunkSide);
  const i32 z = static_cast<i32>(index / voxel::ChunkSide);
  return {{x, y, z}};
}

usize lifecycle_index(voxel::ChunkLifecycleState state) noexcept {
  switch (state) {
    case voxel::ChunkLifecycleState::Unloaded:
      return 0U;
    case voxel::ChunkLifecycleState::Requested:
      return 1U;
    case voxel::ChunkLifecycleState::Loading:
      return 2U;
    case voxel::ChunkLifecycleState::Generating:
      return 3U;
    case voxel::ChunkLifecycleState::Populating:
      return 4U;
    case voxel::ChunkLifecycleState::Loaded:
      return 5U;
    case voxel::ChunkLifecycleState::Active:
      return 6U;
    case voxel::ChunkLifecycleState::MarkedForUnload:
      return 7U;
    case voxel::ChunkLifecycleState::Saving:
      return 8U;
  }
  return 0U;
}

} // namespace arv::world::detail
