#pragma once

#include "arv/core/math.hpp"
#include "arv/physics/collision_query.hpp"
#include "arv/protocol/chunk_streaming.hpp"

#include <span>

namespace arv::client {

class ChunkSectionCollisionQuery final : public physics::ICollisionQuery {
public:
  explicit ChunkSectionCollisionQuery(
      std::span<const protocol::ClientboundChunkSectionData> chunk_sections,
      const voxel::BlockRegistry& block_registry) noexcept;

  [[nodiscard]] bool is_block_loaded(BlockPos pos) const noexcept override;
  [[nodiscard]] voxel::BlockVariantID block_variant(BlockPos pos) const noexcept override;
  [[nodiscard]] voxel::FluidCell fluid_cell(BlockPos pos) const noexcept override;
  [[nodiscard]] const voxel::BlockBody* block_body(BlockPos pos) const noexcept override;
  [[nodiscard]] voxel::BlockPhysicsProperties block_physics(BlockPos pos) const noexcept override;
  [[nodiscard]] bool missing_blocks_are_solid() const noexcept override;

private:
  [[nodiscard]] const protocol::ClientboundChunkSectionData* section_for(
      BlockPos pos) const noexcept;

  std::span<const protocol::ClientboundChunkSectionData> chunk_sections_;
  const voxel::BlockRegistry& block_registry_;
};

} // namespace arv::client
