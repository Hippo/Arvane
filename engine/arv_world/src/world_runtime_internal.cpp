#include "world_runtime_internal.hpp"

#include <cmath>

namespace arv::world::detail {

ChunkSectionCoord chunk_for_position(Vec3f position) noexcept {
  return block_to_chunk({{
      static_cast<i32>(std::floor(position.x)),
      static_cast<i32>(std::floor(position.y)),
      static_cast<i32>(std::floor(position.z)),
  }});
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
