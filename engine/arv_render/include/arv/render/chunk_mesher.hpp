#pragma once

#include "arv/render/render_types.hpp"
#include "arv/voxel/storage.hpp"

#include <array>

namespace arv::render {

[[nodiscard]] constexpr u8 voxel_ambient_occlusion_value(
    bool side0,
    bool side1,
    bool corner) noexcept {
  if (side0 && side1) {
    return 0;
  }
  return static_cast<u8>(3U - static_cast<u8>(side0) - static_cast<u8>(side1) - static_cast<u8>(corner));
}

[[nodiscard]] constexpr bool should_flip_voxel_ao_quad(
    std::array<u8, 4> ao) noexcept {
  return ao[1] + ao[2] > ao[0] + ao[3];
}

[[nodiscard]] ChunkMeshData build_chunk_mesh(
    WorldID world_id,
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry,
    const voxel::BlockRenderData* render_data = nullptr);

} // namespace arv::render
