#pragma once

#include "arv/render/render_types.hpp"
#include "arv/voxel/fluid.hpp"
#include "arv/voxel/storage.hpp"

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace arv::render {

class FluidRenderData {
public:
  void set_fluid_variant(voxel::FluidTypeID fluid_type, voxel::BlockVariantID variant);
  [[nodiscard]] std::optional<voxel::BlockVariantID> variant_for(
      voxel::FluidTypeID fluid_type) const noexcept;

private:
  std::vector<std::pair<voxel::FluidTypeID, voxel::BlockVariantID>> variants_;
};

[[nodiscard]] constexpr u8 voxel_ambient_occlusion_value(
    bool side0,
    bool side1,
    bool corner) noexcept {
  constexpr std::array<u8, 8> AoLut{{3, 2, 2, 0, 2, 1, 1, 0}};
  return AoLut[static_cast<usize>(side0) |
               (static_cast<usize>(side1) << 1U) |
               (static_cast<usize>(corner) << 2U)];
}

[[nodiscard]] constexpr bool should_flip_voxel_ao_quad(
    std::array<u8, 4> ao) noexcept {
  return ao[1] + ao[2] > ao[0] + ao[3];
}

[[nodiscard]] ChunkMeshData build_chunk_mesh(
    WorldID world_id,
    const voxel::ChunkSnapshot& snapshot,
    const voxel::BlockRegistry& registry,
    const voxel::BlockRenderData* render_data = nullptr,
    const FluidRenderData* fluid_render_data = nullptr);

} // namespace arv::render
