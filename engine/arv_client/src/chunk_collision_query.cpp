#include "arv/client/chunk_collision_query.hpp"

#include "arv/voxel/storage.hpp"

#include <algorithm>

namespace arv::client {

ChunkSectionCollisionQuery::ChunkSectionCollisionQuery(
    std::span<const protocol::ClientboundChunkSectionData> chunk_sections,
    const voxel::BlockRegistry& block_registry) noexcept
    : chunk_sections_(chunk_sections),
      block_registry_(block_registry) {
}

bool ChunkSectionCollisionQuery::is_block_loaded(BlockPos pos) const noexcept {
  return section_for(pos) != nullptr;
}

voxel::BlockVariantID ChunkSectionCollisionQuery::block_variant(BlockPos pos) const noexcept {
  const auto* section = section_for(pos);
  if (section == nullptr) {
    return voxel::AirBlockVariantID;
  }
  return section->blocks[voxel::local_index(block_to_local(pos))];
}

voxel::FluidCell ChunkSectionCollisionQuery::fluid_cell(BlockPos pos) const noexcept {
  const auto* section = section_for(pos);
  if (section == nullptr || !section->has_fluids) {
    return voxel::empty_fluid_cell();
  }
  return section->fluids[voxel::local_index(block_to_local(pos))];
}

const voxel::BlockBody* ChunkSectionCollisionQuery::block_body(BlockPos pos) const noexcept {
  const auto* section = section_for(pos);
  if (section == nullptr) {
    return nullptr;
  }
  return block_registry_.block_body(section->blocks[voxel::local_index(block_to_local(pos))]);
}

voxel::BlockPhysicsProperties ChunkSectionCollisionQuery::block_physics(BlockPos pos) const noexcept {
  const auto* section = section_for(pos);
  if (section == nullptr) {
    return {};
  }
  return block_registry_.block_physics(section->blocks[voxel::local_index(block_to_local(pos))]);
}

bool ChunkSectionCollisionQuery::missing_blocks_are_solid() const noexcept {
  return true;
}

const protocol::ClientboundChunkSectionData* ChunkSectionCollisionQuery::section_for(
    BlockPos pos) const noexcept {
  const ChunkSectionCoord coord = block_to_chunk(pos);
  const auto found = std::find_if(
      chunk_sections_.begin(),
      chunk_sections_.end(),
      [&](const protocol::ClientboundChunkSectionData& section) {
        return section.coord == coord;
      });
  return found == chunk_sections_.end() ? nullptr : &*found;
}

} // namespace arv::client
