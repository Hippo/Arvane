#pragma once

#include "arv/physics/collision.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/world_runtime.hpp"

#include <cmath>

namespace arv::world {

class WorldCollisionQuery final : public physics::ICollisionQuery {
public:
  WorldCollisionQuery(
      const WorldRuntime& world,
      const voxel::BlockRegistry& registry,
      bool treat_missing_as_solid = true) noexcept
      : world_(world), registry_(registry), treat_missing_as_solid_(treat_missing_as_solid) {
  }

  [[nodiscard]] bool is_block_loaded(BlockPos pos) const noexcept override {
    return world_.find_chunk(block_to_chunk(pos)) != nullptr;
  }

  [[nodiscard]] voxel::BlockVariantID block_variant(BlockPos pos) const noexcept override {
    return world_.get_block(pos);
  }

  [[nodiscard]] voxel::FluidCell fluid_cell(BlockPos pos) const noexcept override {
    return world_.get_fluid(pos);
  }

  void gather_collision_boxes(const Aabb3f& world_bounds, std::vector<Aabb3f>& out_boxes) const override {
    const i32 min_x = static_cast<i32>(std::floor(world_bounds.min.x));
    const i32 min_y = static_cast<i32>(std::floor(world_bounds.min.y));
    const i32 min_z = static_cast<i32>(std::floor(world_bounds.min.z));
    const i32 max_x = static_cast<i32>(std::ceil(world_bounds.max.x));
    const i32 max_y = static_cast<i32>(std::ceil(world_bounds.max.y));
    const i32 max_z = static_cast<i32>(std::ceil(world_bounds.max.z));
    const i32 span_x = max_x > min_x ? max_x - min_x : 0;
    const i32 span_y = max_y > min_y ? max_y - min_y : 0;
    const i32 span_z = max_z > min_z ? max_z - min_z : 0;
    out_boxes.reserve(
        out_boxes.size() + static_cast<usize>(span_x) * static_cast<usize>(span_y) *
                               static_cast<usize>(span_z));

    for (i32 z = min_z; z < max_z; ++z) {
      for (i32 y = min_y; y < max_y; ++y) {
        for (i32 x = min_x; x < max_x; ++x) {
          gather_block_collision_boxes({{x, y, z}}, out_boxes);
        }
      }
    }
  }

  void gather_block_collision_boxes(BlockPos pos, std::vector<Aabb3f>& out_boxes) const override {
    const voxel::ChunkSection* chunk = world_.find_chunk(block_to_chunk(pos));
    if (chunk == nullptr) {
      if (treat_missing_as_solid_) {
        out_boxes.emplace_back(Aabb3f{
            {static_cast<f32>(pos.value.x),
             static_cast<f32>(pos.value.y),
             static_cast<f32>(pos.value.z)},
            {static_cast<f32>(pos.value.x + 1),
             static_cast<f32>(pos.value.y + 1),
             static_cast<f32>(pos.value.z + 1)}});
      }
      return;
    }

    const voxel::BlockVariantID variant = chunk->block(block_to_local(pos));
    for (const Aabb3f& box : registry_.collision_boxes(variant)) {
      out_boxes.emplace_back(Aabb3f{
          {box.min.x + static_cast<f32>(pos.value.x),
           box.min.y + static_cast<f32>(pos.value.y),
           box.min.z + static_cast<f32>(pos.value.z)},
          {box.max.x + static_cast<f32>(pos.value.x),
           box.max.y + static_cast<f32>(pos.value.y),
           box.max.z + static_cast<f32>(pos.value.z)}});
    }
  }

private:
  const WorldRuntime& world_;
  const voxel::BlockRegistry& registry_;
  bool treat_missing_as_solid_{true};
};

} // namespace arv::world
