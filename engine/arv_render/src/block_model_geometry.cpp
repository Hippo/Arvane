#include "arv/render/block_model_geometry.hpp"

namespace arv::render {

std::array<Vec3i, 4> block_model_box_face_corners(
    const voxel::BlockModelBoxRenderData& box,
    voxel::BlockFace face) noexcept {
  const Vec3i min = box.min;
  const Vec3i max = box.max;
  switch (face) {
    case voxel::BlockFace::PositiveX:
      return {{{max.x, min.y, max.z}, {max.x, min.y, min.z}, {max.x, max.y, max.z}, {max.x, max.y, min.z}}};
    case voxel::BlockFace::NegativeX:
      return {{{min.x, min.y, min.z}, {min.x, min.y, max.z}, {min.x, max.y, min.z}, {min.x, max.y, max.z}}};
    case voxel::BlockFace::PositiveY:
      return {{{min.x, max.y, max.z}, {max.x, max.y, max.z}, {min.x, max.y, min.z}, {max.x, max.y, min.z}}};
    case voxel::BlockFace::NegativeY:
      return {{{max.x, min.y, max.z}, {min.x, min.y, max.z}, {max.x, min.y, min.z}, {min.x, min.y, min.z}}};
    case voxel::BlockFace::PositiveZ:
      return {{{min.x, min.y, max.z}, {max.x, min.y, max.z}, {min.x, max.y, max.z}, {max.x, max.y, max.z}}};
    case voxel::BlockFace::NegativeZ:
      return {{{max.x, min.y, min.z}, {min.x, min.y, min.z}, {max.x, max.y, min.z}, {min.x, max.y, min.z}}};
  }
  return {};
}

} // namespace arv::render
