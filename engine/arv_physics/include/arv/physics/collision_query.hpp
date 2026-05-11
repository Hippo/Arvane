#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/fluid.hpp"

namespace arv::physics {

inline constexpr f32 CollisionEpsilon = 0.001F;

class ICollisionQuery {
public:
  virtual ~ICollisionQuery() = default;

  [[nodiscard]] virtual bool is_block_loaded(BlockPos pos) const noexcept = 0;
  [[nodiscard]] virtual voxel::BlockVariantID block_variant(BlockPos pos) const noexcept = 0;
  [[nodiscard]] virtual voxel::FluidCell fluid_cell(BlockPos pos) const noexcept {
    return voxel::empty_fluid_cell();
  }
  [[nodiscard]] virtual const voxel::BlockBody* block_body(BlockPos pos) const noexcept = 0;
  [[nodiscard]] virtual voxel::BlockPhysicsProperties block_physics(BlockPos /*pos*/) const noexcept {
    return {};
  }
  [[nodiscard]] virtual bool missing_blocks_are_solid() const noexcept {
    return true;
  }
};

} // namespace arv::physics
