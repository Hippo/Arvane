#pragma once

#include "arv/physics/collision.hpp"
#include "arv/physics/raycast.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/world_runtime.hpp"

#include <algorithm>

namespace arv::world {

class WorldCollisionQuery final : public physics::ICollisionQuery {
public:
  WorldCollisionQuery(
      const WorldRuntime& world,
      const voxel::BlockRegistry& registry,
      bool treat_missing_as_solid = true) noexcept
      : registry_(registry),
        search_(world),
        treat_missing_as_solid_(treat_missing_as_solid) {
  }

  [[nodiscard]] bool is_block_loaded(BlockPos pos) const noexcept override {
    return search_.is_chunk_loaded(pos);
  }

  [[nodiscard]] voxel::BlockVariantID block_variant(BlockPos pos) const noexcept override {
    return search_.block_at(pos);
  }

  [[nodiscard]] voxel::FluidCell fluid_cell(BlockPos pos) const noexcept override {
    return search_.fluid_at(pos);
  }

  [[nodiscard]] const voxel::BlockBody* block_body(BlockPos pos) const noexcept override {
    const voxel::ChunkSection* chunk = search_.find_chunk(block_to_chunk(pos));
    if (chunk == nullptr) {
      return nullptr;
    }
    return registry_.block_body(chunk->block(block_to_local(pos)));
  }

  [[nodiscard]] voxel::BlockPhysicsProperties block_physics(BlockPos pos) const noexcept override {
    const voxel::ChunkSection* chunk = search_.find_chunk(block_to_chunk(pos));
    if (chunk == nullptr) {
      return {};
    }
    return registry_.block_physics(chunk->block(block_to_local(pos)));
  }

  [[nodiscard]] bool missing_blocks_are_solid() const noexcept override {
    return treat_missing_as_solid_;
  }

private:
  const voxel::BlockRegistry& registry_;
  mutable WorldSearch search_;
  bool treat_missing_as_solid_{true};
};

[[nodiscard]] inline bool world_cell_contains_fluid(
    const WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    BlockPos pos) noexcept {
  if (!world.get_fluid(pos).empty()) {
    return true;
  }
  const voxel::BlockVariantRecord* record = registry.variant(world.get_block(pos));
  return record != nullptr && record->properties.fluid;
}

[[nodiscard]] inline bool world_has_line_of_sight(
    const WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    Vec3f origin,
    Vec3f target) noexcept {
  const Vec3f delta = target - origin;
  const f32 total = length(delta);
  if (total <= 0.001F) {
    return true;
  }

  WorldCollisionQuery query{world, registry, false};
  return !physics::raycast_blocks(
              query,
              origin,
              delta,
              std::max(0.0F, total - 0.05F))
              .has_value();
}

} // namespace arv::world
