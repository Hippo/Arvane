#include "web_player_movement.hpp"

#include "arv/voxel/storage.hpp"

#include <algorithm>

namespace arv::web {

WebChunkCollisionQuery::WebChunkCollisionQuery(
    std::span<const protocol::ClientboundChunkSectionData> chunk_sections) noexcept
    : chunk_sections_(chunk_sections) {
}

bool WebChunkCollisionQuery::is_block_loaded(BlockPos pos) const noexcept {
  return section_for(pos) != nullptr;
}

voxel::BlockVariantID WebChunkCollisionQuery::block_variant(BlockPos pos) const noexcept {
  const auto* section = section_for(pos);
  if (section == nullptr) {
    return voxel::AirBlockVariantID;
  }
  return section->blocks[voxel::local_index(block_to_local(pos))];
}

voxel::FluidCell WebChunkCollisionQuery::fluid_cell(BlockPos pos) const noexcept {
  const auto* section = section_for(pos);
  if (section == nullptr || !section->has_fluids) {
    return voxel::empty_fluid_cell();
  }
  return section->fluids[voxel::local_index(block_to_local(pos))];
}

const voxel::BlockBody* WebChunkCollisionQuery::block_body(BlockPos pos) const noexcept {
  return block_variant(pos) == voxel::AirBlockVariantID ? nullptr : &full_cube_body();
}

bool WebChunkCollisionQuery::missing_blocks_are_solid() const noexcept {
  return true;
}

const protocol::ClientboundChunkSectionData* WebChunkCollisionQuery::section_for(
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

const voxel::BlockBody& WebChunkCollisionQuery::full_cube_body() noexcept {
  static const voxel::BlockBody body{{
      Aabb3f{{0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}}}};
  return body;
}

} // namespace arv::web
