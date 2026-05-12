#pragma once

#include "arv/render/render_types.hpp"

#include <array>
#include <algorithm>
#include <cmath>

namespace arv::render {

[[nodiscard]] inline f32 render_view_aspect_ratio(const RenderView& view) noexcept {
  if (view.viewport_width == 0U || view.viewport_height == 0U) {
    return 1.0F;
  }
  return static_cast<f32>(view.viewport_width) /
         static_cast<f32>(view.viewport_height);
}

[[nodiscard]] inline std::array<Vec3f, 8> chunk_section_aabb_corners(
    ChunkSectionCoord coord) noexcept {
  const Vec3f min{
      static_cast<f32>(coord.value.x * ChunkSize),
      static_cast<f32>(coord.value.y * ChunkSize),
      static_cast<f32>(coord.value.z * ChunkSize)};
  const Vec3f max{
      min.x + static_cast<f32>(ChunkSize),
      min.y + static_cast<f32>(ChunkSize),
      min.z + static_cast<f32>(ChunkSize)};
  return {{
      {min.x, min.y, min.z},
      {max.x, min.y, min.z},
      {min.x, max.y, min.z},
      {max.x, max.y, min.z},
      {min.x, min.y, max.z},
      {max.x, min.y, max.z},
      {min.x, max.y, max.z},
      {max.x, max.y, max.z},
  }};
}

[[nodiscard]] inline bool chunk_aabb_intersects_view(
    const RenderView& view,
    ChunkSectionCoord coord) noexcept {
  const Vec3f forward = normalize_or_zero(view.forward);
  const Vec3f right = normalize_or_zero(view.right);
  const Vec3f up = normalize_or_zero(view.up);
  if (dot(forward, forward) <= 0.0F ||
      dot(right, right) <= 0.0F ||
      dot(up, up) <= 0.0F) {
    return true;
  }

  const f32 fov_y_degrees =
      std::isfinite(view.fov_y_degrees)
          ? std::clamp(view.fov_y_degrees, 1.0F, 170.0F)
          : 70.0F;
  const f32 tan_y = std::tan(degrees_to_radians(fov_y_degrees) * 0.5F);
  const f32 tan_x = tan_y * std::max(render_view_aspect_ratio(view), 0.01F);
  const f32 near_plane =
      std::isfinite(view.near_plane) ? std::max(view.near_plane, 0.001F) : 0.001F;
  const f32 far_plane =
      std::isfinite(view.far_plane)
          ? std::max(view.far_plane, near_plane + 1.0F)
          : near_plane + 1.0F;

  bool all_before_near = true;
  bool all_after_far = true;
  bool all_left = true;
  bool all_right = true;
  bool all_below = true;
  bool all_above = true;

  for (const Vec3f corner : chunk_section_aabb_corners(coord)) {
    const Vec3f relative = corner - view.position;
    const f32 view_x = dot(relative, right);
    const f32 view_y = dot(relative, up);
    const f32 view_z = dot(relative, forward);

    all_before_near = all_before_near && view_z < near_plane;
    all_after_far = all_after_far && view_z > far_plane;
    all_left = all_left && view_x < -view_z * tan_x;
    all_right = all_right && view_x > view_z * tan_x;
    all_below = all_below && view_y < -view_z * tan_y;
    all_above = all_above && view_y > view_z * tan_y;
  }

  return !(all_before_near || all_after_far || all_left || all_right ||
           all_below || all_above);
}

} // namespace arv::render
