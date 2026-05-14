#pragma once

#include "arv/core/math.hpp"
#include "arv/voxel/block.hpp"

#include <array>

namespace arv::render {

[[nodiscard]] std::array<Vec3i, 4> block_model_box_face_corners(
    const voxel::BlockModelBoxRenderData& box,
    voxel::BlockFace face) noexcept;

} // namespace arv::render
