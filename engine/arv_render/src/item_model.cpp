#include "arv/render/item_model.hpp"

#include <algorithm>
#include <array>
#include <tuple>
#include <vector>

namespace arv::render {
namespace {

struct Quad {
  std::array<Vec3f, 4> positions{};
  std::array<Vec2f, 4> uvs{};
  Vec3f color{1.0F, 1.0F, 1.0F};
  u32 atlas_page{};
};

enum class EdgeAxis : u8 {
  X,
  Y,
  Z,
};

struct EdgeSegment {
  EdgeAxis axis{};
  i32 a{};
  i32 b{};
  i32 start{};
  i32 end{};
};

using OccupancyGrid = std::array<bool, 16U * 16U * 16U>;

[[nodiscard]] bool valid_texture_input(const ItemTextureMeshInput& input) noexcept {
  return input.width > 0U && input.height > 0U &&
         input.rgba_pixels.size() >=
             static_cast<usize>(input.width) * static_cast<usize>(input.height) * 4U;
}

[[nodiscard]] bool visible_pixel(
    const ItemTextureMeshInput& input,
    i32 x,
    i32 y) noexcept {
  if (x < 0 || y < 0 || x >= static_cast<i32>(input.width) ||
      y >= static_cast<i32>(input.height)) {
    return false;
  }
  const usize offset =
      (static_cast<usize>(y) * input.width + static_cast<usize>(x)) * 4U;
  return input.rgba_pixels[offset + 3U] != 0U;
}

void append_quad(EntityModelAsset& asset, const Quad& quad) {
  const u32 base = static_cast<u32>(asset.vertices.size());
  for (usize index = 0; index < quad.positions.size(); ++index) {
    asset.vertices.push_back({
        quad.positions[index],
        quad.uvs[index],
        quad.color,
        0U,
        0U,
        static_cast<u32>(TextureAtlasDomain::Entities),
        quad.atlas_page});
  }
  asset.indices.insert(
      asset.indices.end(),
      {base + 0U, base + 1U, base + 2U, base + 2U, base + 1U, base + 3U});
}

[[nodiscard]] Vec3f face_color(voxel::BlockFace face) noexcept {
  switch (face) {
    case voxel::BlockFace::PositiveY:
      return {1.0F, 1.0F, 1.0F};
    case voxel::BlockFace::NegativeY:
      return {0.55F, 0.55F, 0.55F};
    case voxel::BlockFace::PositiveX:
    case voxel::BlockFace::NegativeX:
      return {0.78F, 0.78F, 0.78F};
    case voxel::BlockFace::PositiveZ:
    case voxel::BlockFace::NegativeZ:
      return {0.86F, 0.86F, 0.86F};
  }
  return {1.0F, 1.0F, 1.0F};
}

[[nodiscard]] f32 model_x(u32 x, u32 width) noexcept {
  return -8.0F + 16.0F * static_cast<f32>(x) / static_cast<f32>(std::max(width, 1U));
}

[[nodiscard]] f32 model_y(u32 y, u32 height) noexcept {
  return 8.0F - 16.0F * static_cast<f32>(y) / static_cast<f32>(std::max(height, 1U));
}

[[nodiscard]] std::array<Vec2f, 4> pixel_uvs(
    const ItemTextureMeshInput& input,
    u32 x,
    u32 y) noexcept {
  const f32 u0 = static_cast<f32>(input.atlas_pixel_min.x + static_cast<i32>(x));
  const f32 v0 = static_cast<f32>(input.atlas_pixel_min.y + static_cast<i32>(y));
  const f32 u1 = u0 + 1.0F;
  const f32 v1 = v0 + 1.0F;
  return {{{u0, v1}, {u1, v1}, {u0, v0}, {u1, v0}}};
}

void append_texture_pixel(
    EntityModelAsset& asset,
    const ItemTextureMeshInput& input,
    u32 x,
    u32 y) {
  const f32 x0 = model_x(x, input.width);
  const f32 x1 = model_x(x + 1U, input.width);
  const f32 y0 = model_y(y + 1U, input.height);
  const f32 y1 = model_y(y, input.height);
  const f32 z0 = -std::max(input.thickness, 0.25F) * 0.5F;
  const f32 z1 = -z0;
  const std::array<Vec2f, 4> uvs = pixel_uvs(input, x, y);

  append_quad(
      asset,
      Quad{{{{x0, y0, z1}, {x1, y0, z1}, {x0, y1, z1}, {x1, y1, z1}}},
           uvs,
           face_color(voxel::BlockFace::PositiveZ),
           input.atlas_page});
  append_quad(
      asset,
      Quad{{{{x1, y0, z0}, {x0, y0, z0}, {x1, y1, z0}, {x0, y1, z0}}},
           std::array<Vec2f, 4>{{uvs[1], uvs[0], uvs[3], uvs[2]}},
           face_color(voxel::BlockFace::NegativeZ),
           input.atlas_page});

  if (!visible_pixel(input, static_cast<i32>(x) - 1, static_cast<i32>(y))) {
    append_quad(
        asset,
        Quad{{{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z0}, {x0, y1, z1}}},
             std::array<Vec2f, 4>{{uvs[0], uvs[1], uvs[2], uvs[3]}},
             face_color(voxel::BlockFace::NegativeX),
             input.atlas_page});
  }
  if (!visible_pixel(input, static_cast<i32>(x) + 1, static_cast<i32>(y))) {
    append_quad(
        asset,
        Quad{{{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z1}, {x1, y1, z0}}},
             std::array<Vec2f, 4>{{uvs[1], uvs[0], uvs[3], uvs[2]}},
             face_color(voxel::BlockFace::PositiveX),
             input.atlas_page});
  }
  if (!visible_pixel(input, static_cast<i32>(x), static_cast<i32>(y) - 1)) {
    append_quad(
        asset,
        Quad{{{{x0, y1, z1}, {x1, y1, z1}, {x0, y1, z0}, {x1, y1, z0}}},
             std::array<Vec2f, 4>{{uvs[2], uvs[3], uvs[0], uvs[1]}},
             face_color(voxel::BlockFace::PositiveY),
             input.atlas_page});
  }
  if (!visible_pixel(input, static_cast<i32>(x), static_cast<i32>(y) + 1)) {
    append_quad(
        asset,
        Quad{{{{x0, y0, z0}, {x1, y0, z0}, {x0, y0, z1}, {x1, y0, z1}}},
             std::array<Vec2f, 4>{{uvs[0], uvs[1], uvs[2], uvs[3]}},
             face_color(voxel::BlockFace::NegativeY),
             input.atlas_page});
  }
}

struct CubeFaceGeometry {
  voxel::BlockFace face{};
  std::array<Vec3f, 4> corners{};
};

[[nodiscard]] constexpr Vec3f centered(i32 x, i32 y, i32 z) noexcept {
  return {
      static_cast<f32>(x) - 8.0F,
      static_cast<f32>(y) - 8.0F,
      static_cast<f32>(z) - 8.0F};
}

[[nodiscard]] constexpr Vec3f centered(f32 x, f32 y, f32 z) noexcept {
  return {x - 8.0F, y - 8.0F, z - 8.0F};
}

constexpr std::array<CubeFaceGeometry, voxel::BlockFaceCount> CubeFaces{{
    {
        voxel::BlockFace::PositiveX,
        std::array<Vec3f, 4>{{centered(16, 0, 16), centered(16, 0, 0), centered(16, 16, 16), centered(16, 16, 0)}},
    },
    {
        voxel::BlockFace::NegativeX,
        std::array<Vec3f, 4>{{centered(0, 0, 0), centered(0, 0, 16), centered(0, 16, 0), centered(0, 16, 16)}},
    },
    {
        voxel::BlockFace::PositiveY,
        std::array<Vec3f, 4>{{centered(0, 16, 16), centered(16, 16, 16), centered(0, 16, 0), centered(16, 16, 0)}},
    },
    {
        voxel::BlockFace::NegativeY,
        std::array<Vec3f, 4>{{centered(16, 0, 16), centered(0, 0, 16), centered(16, 0, 0), centered(0, 0, 0)}},
    },
    {
        voxel::BlockFace::PositiveZ,
        std::array<Vec3f, 4>{{centered(0, 0, 16), centered(16, 0, 16), centered(0, 16, 16), centered(16, 16, 16)}},
    },
    {
        voxel::BlockFace::NegativeZ,
        std::array<Vec3f, 4>{{centered(0, 0, 0), centered(16, 0, 0), centered(0, 16, 0), centered(16, 16, 0)}},
    },
}};

[[nodiscard]] std::array<Vec3f, 4> model_box_face_corners(
    const voxel::BlockModelBoxRenderData& box,
    voxel::BlockFace face) noexcept {
  const Vec3i min = box.min;
  const Vec3i max = box.max;
  switch (face) {
    case voxel::BlockFace::PositiveX:
      return {{centered(max.x, min.y, max.z), centered(max.x, min.y, min.z), centered(max.x, max.y, max.z), centered(max.x, max.y, min.z)}};
    case voxel::BlockFace::NegativeX:
      return {{centered(min.x, min.y, min.z), centered(min.x, min.y, max.z), centered(min.x, max.y, min.z), centered(min.x, max.y, max.z)}};
    case voxel::BlockFace::PositiveY:
      return {{centered(min.x, max.y, max.z), centered(max.x, max.y, max.z), centered(min.x, max.y, min.z), centered(max.x, max.y, min.z)}};
    case voxel::BlockFace::NegativeY:
      return {{centered(max.x, min.y, max.z), centered(min.x, min.y, max.z), centered(max.x, min.y, min.z), centered(min.x, min.y, min.z)}};
    case voxel::BlockFace::PositiveZ:
      return {{centered(min.x, min.y, max.z), centered(max.x, min.y, max.z), centered(min.x, max.y, max.z), centered(max.x, max.y, max.z)}};
    case voxel::BlockFace::NegativeZ:
      return {{centered(max.x, min.y, min.z), centered(min.x, min.y, min.z), centered(max.x, max.y, min.z), centered(min.x, max.y, min.z)}};
  }
  return {};
}

[[nodiscard]] std::array<Vec3f, 4> model_box_face_corners_inflated(
    const voxel::BlockModelBoxRenderData& box,
    voxel::BlockFace face,
    f32 inflate) noexcept {
  const Vec3f min{
      static_cast<f32>(box.min.x) - inflate,
      static_cast<f32>(box.min.y) - inflate,
      static_cast<f32>(box.min.z) - inflate};
  const Vec3f max{
      static_cast<f32>(box.max.x) + inflate,
      static_cast<f32>(box.max.y) + inflate,
      static_cast<f32>(box.max.z) + inflate};
  switch (face) {
    case voxel::BlockFace::PositiveX:
      return {{centered(max.x, min.y, max.z), centered(max.x, min.y, min.z), centered(max.x, max.y, max.z), centered(max.x, max.y, min.z)}};
    case voxel::BlockFace::NegativeX:
      return {{centered(min.x, min.y, min.z), centered(min.x, min.y, max.z), centered(min.x, max.y, min.z), centered(min.x, max.y, max.z)}};
    case voxel::BlockFace::PositiveY:
      return {{centered(min.x, max.y, max.z), centered(max.x, max.y, max.z), centered(min.x, max.y, min.z), centered(max.x, max.y, min.z)}};
    case voxel::BlockFace::NegativeY:
      return {{centered(max.x, min.y, max.z), centered(min.x, min.y, max.z), centered(max.x, min.y, min.z), centered(min.x, min.y, min.z)}};
    case voxel::BlockFace::PositiveZ:
      return {{centered(min.x, min.y, max.z), centered(max.x, min.y, max.z), centered(min.x, max.y, max.z), centered(max.x, max.y, max.z)}};
    case voxel::BlockFace::NegativeZ:
      return {{centered(max.x, min.y, min.z), centered(min.x, min.y, min.z), centered(max.x, max.y, min.z), centered(min.x, max.y, min.z)}};
  }
  return {};
}

[[nodiscard]] std::array<Vec3f, 4> cube_face_corners_inflated(
    voxel::BlockFace face,
    f32 inflate) noexcept {
  const f32 min = -inflate;
  const f32 max = 16.0F + inflate;
  switch (face) {
    case voxel::BlockFace::PositiveX:
      return {{centered(max, min, max), centered(max, min, min), centered(max, max, max), centered(max, max, min)}};
    case voxel::BlockFace::NegativeX:
      return {{centered(min, min, min), centered(min, min, max), centered(min, max, min), centered(min, max, max)}};
    case voxel::BlockFace::PositiveY:
      return {{centered(min, max, max), centered(max, max, max), centered(min, max, min), centered(max, max, min)}};
    case voxel::BlockFace::NegativeY:
      return {{centered(max, min, max), centered(min, min, max), centered(max, min, min), centered(min, min, min)}};
    case voxel::BlockFace::PositiveZ:
      return {{centered(min, min, max), centered(max, min, max), centered(min, max, max), centered(max, max, max)}};
    case voxel::BlockFace::NegativeZ:
      return {{centered(max, min, min), centered(min, min, min), centered(max, max, min), centered(min, max, min)}};
  }
  return {};
}

[[nodiscard]] std::array<Vec2f, 4> to_vec2f(std::array<Vec2i, 4> uvs) noexcept {
  std::array<Vec2f, 4> result{};
  for (usize index = 0; index < uvs.size(); ++index) {
    result[index] = {static_cast<f32>(uvs[index].x), static_cast<f32>(uvs[index].y)};
  }
  return result;
}

[[nodiscard]] constexpr usize grid_index(i32 x, i32 y, i32 z) noexcept {
  return static_cast<usize>(x) +
         16U * (static_cast<usize>(y) + 16U * static_cast<usize>(z));
}

[[nodiscard]] bool occupied(const OccupancyGrid& grid, i32 x, i32 y, i32 z) noexcept {
  if (x < 0 || y < 0 || z < 0 || x >= 16 || y >= 16 || z >= 16) {
    return false;
  }
  return grid[grid_index(x, y, z)];
}

[[nodiscard]] OccupancyGrid make_occupancy_grid(
    std::span<const voxel::BlockModelBoxRenderData> model_boxes) {
  OccupancyGrid grid{};
  if (model_boxes.empty()) {
    grid.fill(true);
    return grid;
  }

  for (const voxel::BlockModelBoxRenderData& box : model_boxes) {
    const i32 min_x = std::clamp(box.min.x, 0, 16);
    const i32 min_y = std::clamp(box.min.y, 0, 16);
    const i32 min_z = std::clamp(box.min.z, 0, 16);
    const i32 max_x = std::clamp(box.max.x, 0, 16);
    const i32 max_y = std::clamp(box.max.y, 0, 16);
    const i32 max_z = std::clamp(box.max.z, 0, 16);
    for (i32 z = min_z; z < max_z; ++z) {
      for (i32 y = min_y; y < max_y; ++y) {
        for (i32 x = min_x; x < max_x; ++x) {
          grid[grid_index(x, y, z)] = true;
        }
      }
    }
  }
  return grid;
}

[[nodiscard]] bool exposed_face(
    const OccupancyGrid& grid,
    i32 x,
    i32 y,
    i32 z,
    voxel::BlockFace face) noexcept {
  if (!occupied(grid, x, y, z)) {
    return false;
  }
  switch (face) {
    case voxel::BlockFace::PositiveX:
      return !occupied(grid, x + 1, y, z);
    case voxel::BlockFace::NegativeX:
      return !occupied(grid, x - 1, y, z);
    case voxel::BlockFace::PositiveY:
      return !occupied(grid, x, y + 1, z);
    case voxel::BlockFace::NegativeY:
      return !occupied(grid, x, y - 1, z);
    case voxel::BlockFace::PositiveZ:
      return !occupied(grid, x, y, z + 1);
    case voxel::BlockFace::NegativeZ:
      return !occupied(grid, x, y, z - 1);
  }
  return false;
}

[[nodiscard]] Vec2f bilinear_uv(
    const std::array<std::array<Vec2i, 4>, voxel::BlockFaceCount>& face_uvs,
    voxel::BlockFace face,
    f32 s,
    f32 t) noexcept {
  const std::array<Vec2i, 4>& uvs = face_uvs[voxel::face_index(face)];
  const f32 u0 = static_cast<f32>(uvs[0].x);
  const f32 v0 = static_cast<f32>(uvs[0].y);
  const f32 u1 = static_cast<f32>(uvs[1].x);
  const f32 v1 = static_cast<f32>(uvs[1].y);
  const f32 u2 = static_cast<f32>(uvs[2].x);
  const f32 v2 = static_cast<f32>(uvs[2].y);
  const f32 u3 = static_cast<f32>(uvs[3].x);
  const f32 v3 = static_cast<f32>(uvs[3].y);
  const f32 a = (1.0F - s) * (1.0F - t);
  const f32 b = s * (1.0F - t);
  const f32 c = (1.0F - s) * t;
  const f32 d = s * t;
  return {
      u0 * a + u1 * b + u2 * c + u3 * d,
      v0 * a + v1 * b + v2 * c + v3 * d};
}

[[nodiscard]] std::pair<f32, f32> face_st(voxel::BlockFace face, Vec3f p) noexcept {
  switch (face) {
    case voxel::BlockFace::PositiveX:
      return {(16.0F - p.z) / 16.0F, p.y / 16.0F};
    case voxel::BlockFace::NegativeX:
      return {p.z / 16.0F, p.y / 16.0F};
    case voxel::BlockFace::PositiveY:
      return {p.x / 16.0F, (16.0F - p.z) / 16.0F};
    case voxel::BlockFace::NegativeY:
      return {(16.0F - p.x) / 16.0F, (16.0F - p.z) / 16.0F};
    case voxel::BlockFace::PositiveZ:
      return {p.x / 16.0F, p.y / 16.0F};
    case voxel::BlockFace::NegativeZ:
      return {(16.0F - p.x) / 16.0F, p.y / 16.0F};
  }
  return {};
}

[[nodiscard]] Vec2f projected_uv(
    const std::array<std::array<Vec2i, 4>, voxel::BlockFaceCount>& face_uvs,
    voxel::BlockFace face,
    Vec3f p) noexcept {
  const auto [s, t] = face_st(face, p);
  return bilinear_uv(face_uvs, face, std::clamp(s, 0.0F, 1.0F), std::clamp(t, 0.0F, 1.0F));
}

void append_projected_cell_face(
    EntityModelAsset& asset,
    const std::array<std::array<Vec2i, 4>, voxel::BlockFaceCount>& face_uvs,
    const std::array<u16, voxel::BlockFaceCount>& face_pages,
    i32 x,
    i32 y,
    i32 z,
    voxel::BlockFace face,
    f32 inflate,
    Vec3f color) {
  const f32 x0 = static_cast<f32>(x);
  const f32 x1 = static_cast<f32>(x + 1);
  const f32 y0 = static_cast<f32>(y);
  const f32 y1 = static_cast<f32>(y + 1);
  const f32 z0 = static_cast<f32>(z);
  const f32 z1 = static_cast<f32>(z + 1);
  std::array<Vec3f, 4> p{};
  switch (face) {
    case voxel::BlockFace::PositiveX:
      p = {{{x1 + inflate, y0, z1}, {x1 + inflate, y0, z0}, {x1 + inflate, y1, z1}, {x1 + inflate, y1, z0}}};
      break;
    case voxel::BlockFace::NegativeX:
      p = {{{x0 - inflate, y0, z0}, {x0 - inflate, y0, z1}, {x0 - inflate, y1, z0}, {x0 - inflate, y1, z1}}};
      break;
    case voxel::BlockFace::PositiveY:
      p = {{{x0, y1 + inflate, z1}, {x1, y1 + inflate, z1}, {x0, y1 + inflate, z0}, {x1, y1 + inflate, z0}}};
      break;
    case voxel::BlockFace::NegativeY:
      p = {{{x1, y0 - inflate, z1}, {x0, y0 - inflate, z1}, {x1, y0 - inflate, z0}, {x0, y0 - inflate, z0}}};
      break;
    case voxel::BlockFace::PositiveZ:
      p = {{{x0, y0, z1 + inflate}, {x1, y0, z1 + inflate}, {x0, y1, z1 + inflate}, {x1, y1, z1 + inflate}}};
      break;
    case voxel::BlockFace::NegativeZ:
      p = {{{x1, y0, z0 - inflate}, {x0, y0, z0 - inflate}, {x1, y1, z0 - inflate}, {x0, y1, z0 - inflate}}};
      break;
  }

  append_quad(
      asset,
      {{centered(p[0].x, p[0].y, p[0].z),
        centered(p[1].x, p[1].y, p[1].z),
        centered(p[2].x, p[2].y, p[2].z),
        centered(p[3].x, p[3].y, p[3].z)},
       {projected_uv(face_uvs, face, p[0]),
        projected_uv(face_uvs, face, p[1]),
        projected_uv(face_uvs, face, p[2]),
        projected_uv(face_uvs, face, p[3])},
       color,
       face_pages[voxel::face_index(face)]});
}

void append_box(
    EntityModelAsset& asset,
    Vec3f min,
    Vec3f max,
    const std::array<Vec2f, 4>& uvs,
    Vec3f color,
    u16 atlas_page) {
  if (max.x <= min.x || max.y <= min.y || max.z <= min.z) {
    return;
  }
  const Vec3f p000 = centered(min.x, min.y, min.z);
  const Vec3f p001 = centered(min.x, min.y, max.z);
  const Vec3f p010 = centered(min.x, max.y, min.z);
  const Vec3f p011 = centered(min.x, max.y, max.z);
  const Vec3f p100 = centered(max.x, min.y, min.z);
  const Vec3f p101 = centered(max.x, min.y, max.z);
  const Vec3f p110 = centered(max.x, max.y, min.z);
  const Vec3f p111 = centered(max.x, max.y, max.z);

  append_quad(asset, {{{p101, p100, p111, p110}}, uvs, color, atlas_page});
  append_quad(asset, {{{p000, p001, p010, p011}}, uvs, color, atlas_page});
  append_quad(asset, {{{p011, p111, p010, p110}}, uvs, color, atlas_page});
  append_quad(asset, {{{p100, p000, p110, p010}}, uvs, color, atlas_page});
  append_quad(asset, {{{p001, p101, p011, p111}}, uvs, color, atlas_page});
  append_quad(asset, {{{p100, p000, p101, p001}}, uvs, color, atlas_page});
}

void add_edge_segment(
    std::vector<EdgeSegment>& edges,
    EdgeAxis axis,
    i32 a,
    i32 b,
    i32 start,
    i32 end) {
  if (start == end) {
    return;
  }
  if (end < start) {
    std::swap(start, end);
  }
  EdgeSegment edge{axis, a, b, start, end};
  const auto found = std::find_if(edges.begin(), edges.end(), [&](const EdgeSegment& existing) {
    return existing.axis == edge.axis && existing.a == edge.a && existing.b == edge.b &&
           existing.start == edge.start && existing.end == edge.end;
  });
  if (found == edges.end()) {
    edges.push_back(edge);
  }
}

void add_exposed_face_edges(
    const OccupancyGrid& grid,
    std::vector<EdgeSegment>& edges,
    i32 x,
    i32 y,
    i32 z,
    voxel::BlockFace face) {
  switch (face) {
    case voxel::BlockFace::PositiveX: {
      const i32 plane = x + 1;
      if (!exposed_face(grid, x, y - 1, z, face)) {
        add_edge_segment(edges, EdgeAxis::Z, plane, y, z, z + 1);
      }
      if (!exposed_face(grid, x, y + 1, z, face)) {
        add_edge_segment(edges, EdgeAxis::Z, plane, y + 1, z, z + 1);
      }
      if (!exposed_face(grid, x, y, z - 1, face)) {
        add_edge_segment(edges, EdgeAxis::Y, plane, z, y, y + 1);
      }
      if (!exposed_face(grid, x, y, z + 1, face)) {
        add_edge_segment(edges, EdgeAxis::Y, plane, z + 1, y, y + 1);
      }
      break;
    }
    case voxel::BlockFace::NegativeX: {
      const i32 plane = x;
      if (!exposed_face(grid, x, y - 1, z, face)) {
        add_edge_segment(edges, EdgeAxis::Z, plane, y, z, z + 1);
      }
      if (!exposed_face(grid, x, y + 1, z, face)) {
        add_edge_segment(edges, EdgeAxis::Z, plane, y + 1, z, z + 1);
      }
      if (!exposed_face(grid, x, y, z - 1, face)) {
        add_edge_segment(edges, EdgeAxis::Y, plane, z, y, y + 1);
      }
      if (!exposed_face(grid, x, y, z + 1, face)) {
        add_edge_segment(edges, EdgeAxis::Y, plane, z + 1, y, y + 1);
      }
      break;
    }
    case voxel::BlockFace::PositiveY: {
      const i32 plane = y + 1;
      if (!exposed_face(grid, x - 1, y, z, face)) {
        add_edge_segment(edges, EdgeAxis::Z, x, plane, z, z + 1);
      }
      if (!exposed_face(grid, x + 1, y, z, face)) {
        add_edge_segment(edges, EdgeAxis::Z, x + 1, plane, z, z + 1);
      }
      if (!exposed_face(grid, x, y, z - 1, face)) {
        add_edge_segment(edges, EdgeAxis::X, plane, z, x, x + 1);
      }
      if (!exposed_face(grid, x, y, z + 1, face)) {
        add_edge_segment(edges, EdgeAxis::X, plane, z + 1, x, x + 1);
      }
      break;
    }
    case voxel::BlockFace::NegativeY: {
      const i32 plane = y;
      if (!exposed_face(grid, x - 1, y, z, face)) {
        add_edge_segment(edges, EdgeAxis::Z, x, plane, z, z + 1);
      }
      if (!exposed_face(grid, x + 1, y, z, face)) {
        add_edge_segment(edges, EdgeAxis::Z, x + 1, plane, z, z + 1);
      }
      if (!exposed_face(grid, x, y, z - 1, face)) {
        add_edge_segment(edges, EdgeAxis::X, plane, z, x, x + 1);
      }
      if (!exposed_face(grid, x, y, z + 1, face)) {
        add_edge_segment(edges, EdgeAxis::X, plane, z + 1, x, x + 1);
      }
      break;
    }
    case voxel::BlockFace::PositiveZ: {
      const i32 plane = z + 1;
      if (!exposed_face(grid, x - 1, y, z, face)) {
        add_edge_segment(edges, EdgeAxis::Y, x, plane, y, y + 1);
      }
      if (!exposed_face(grid, x + 1, y, z, face)) {
        add_edge_segment(edges, EdgeAxis::Y, x + 1, plane, y, y + 1);
      }
      if (!exposed_face(grid, x, y - 1, z, face)) {
        add_edge_segment(edges, EdgeAxis::X, y, plane, x, x + 1);
      }
      if (!exposed_face(grid, x, y + 1, z, face)) {
        add_edge_segment(edges, EdgeAxis::X, y + 1, plane, x, x + 1);
      }
      break;
    }
    case voxel::BlockFace::NegativeZ: {
      const i32 plane = z;
      if (!exposed_face(grid, x - 1, y, z, face)) {
        add_edge_segment(edges, EdgeAxis::Y, x, plane, y, y + 1);
      }
      if (!exposed_face(grid, x + 1, y, z, face)) {
        add_edge_segment(edges, EdgeAxis::Y, x + 1, plane, y, y + 1);
      }
      if (!exposed_face(grid, x, y - 1, z, face)) {
        add_edge_segment(edges, EdgeAxis::X, y, plane, x, x + 1);
      }
      if (!exposed_face(grid, x, y + 1, z, face)) {
        add_edge_segment(edges, EdgeAxis::X, y + 1, plane, x, x + 1);
      }
      break;
    }
  }
}

[[nodiscard]] std::vector<EdgeSegment> merged_edges(std::vector<EdgeSegment> edges) {
  std::sort(edges.begin(), edges.end(), [](const EdgeSegment& lhs, const EdgeSegment& rhs) {
    return std::tuple{lhs.axis, lhs.a, lhs.b, lhs.start, lhs.end} <
           std::tuple{rhs.axis, rhs.a, rhs.b, rhs.start, rhs.end};
  });

  std::vector<EdgeSegment> merged;
  for (const EdgeSegment& edge : edges) {
    if (!merged.empty()) {
      EdgeSegment& last = merged.back();
      if (last.axis == edge.axis && last.a == edge.a && last.b == edge.b &&
          edge.start <= last.end) {
        last.end = std::max(last.end, edge.end);
        continue;
      }
    }
    merged.push_back(edge);
  }
  return merged;
}

void append_edge_box(
    EntityModelAsset& asset,
    const EdgeSegment& edge,
    f32 thickness,
    f32 inflate,
    const std::array<Vec2f, 4>& uvs,
    Vec3f color,
    u16 atlas_page) {
  const f32 t = std::max(thickness, 0.01F) * 0.5F;
  const f32 a = static_cast<f32>(edge.a);
  const f32 b = static_cast<f32>(edge.b);
  const f32 start = static_cast<f32>(edge.start);
  const f32 end = static_cast<f32>(edge.end);
  switch (edge.axis) {
    case EdgeAxis::X:
      append_box(
          asset,
          {start - inflate, a - t, b - t},
          {end + inflate, a + t, b + t},
          uvs,
          color,
          atlas_page);
      break;
    case EdgeAxis::Y:
      append_box(
          asset,
          {a - t, start - inflate, b - t},
          {a + t, end + inflate, b + t},
          uvs,
          color,
          atlas_page);
      break;
    case EdgeAxis::Z:
      append_box(
          asset,
          {a - t, b - t, start - inflate},
          {a + t, b + t, end + inflate},
          uvs,
          color,
          atlas_page);
      break;
  }
}

} // namespace

EntityModelAsset build_extruded_texture_item_model(const ItemTextureMeshInput& input) {
  EntityModelAsset asset;
  asset.id = input.id;
  asset.name = input.name;
  if (!valid_texture_input(input)) {
    return asset;
  }

  for (u32 y = 0; y < input.height; ++y) {
    for (u32 x = 0; x < input.width; ++x) {
      if (visible_pixel(input, static_cast<i32>(x), static_cast<i32>(y))) {
        append_texture_pixel(asset, input, x, y);
      }
    }
  }

  return asset;
}

EntityModelAsset build_block_item_model(const ItemBlockModelInput& input) {
  EntityModelAsset asset;
  asset.id = input.id;
  asset.name = input.name;

  if (!input.model_boxes.empty()) {
    for (const voxel::BlockModelBoxRenderData& box : input.model_boxes) {
      for (const voxel::BlockFace face : voxel::AllBlockFaces) {
        const usize index = voxel::face_index(face);
        if (!box.faces[index].has_value()) {
          continue;
        }
        append_quad(
            asset,
            {model_box_face_corners(box, face),
             to_vec2f(box.faces[index]->uvs),
             face_color(face),
             box.faces[index]->page});
      }
    }
    return asset;
  }

  for (const CubeFaceGeometry& face : CubeFaces) {
    const usize index = voxel::face_index(face.face);
    if (!input.face_valid[index]) {
      continue;
    }
    append_quad(
        asset,
        {face.corners,
         to_vec2f(input.face_uvs[index]),
         face_color(face.face),
         input.face_pages[index]});
  }

  return asset;
}

EntityModelAsset build_block_break_overlay_model(const BlockBreakOverlayModelInput& input) {
  EntityModelAsset asset;
  asset.id = input.id;
  asset.name = input.name;

  const f32 inflate = std::max(input.inflate_sixteenths, 0.0F);
  const OccupancyGrid grid = make_occupancy_grid(input.model_boxes);
  for (i32 z = 0; z < 16; ++z) {
    for (i32 y = 0; y < 16; ++y) {
      for (i32 x = 0; x < 16; ++x) {
        if (!occupied(grid, x, y, z)) {
          continue;
        }
        for (const voxel::BlockFace face : voxel::AllBlockFaces) {
          if (!input.model_boxes.empty() || input.face_valid[voxel::face_index(face)]) {
            if (exposed_face(grid, x, y, z, face)) {
              append_projected_cell_face(
                  asset,
                  input.face_uvs,
                  input.face_pages,
                  x,
                  y,
                  z,
                  face,
                  inflate,
                  {1.0F, 1.0F, 1.0F});
            }
          }
        }
      }
    }
  }

  return asset;
}

EntityModelAsset build_block_outline_model(const BlockOutlineModelInput& input) {
  EntityModelAsset asset;
  asset.id = input.id;
  asset.name = input.name;

  const std::array<Vec2f, 4> uvs = to_vec2f(input.uvs);
  const f32 thickness = std::max(input.thickness_sixteenths, 0.01F);
  const f32 inflate = std::max(input.inflate_sixteenths, 0.0F);
  const OccupancyGrid grid = make_occupancy_grid(input.model_boxes);
  std::vector<EdgeSegment> edge_segments;
  edge_segments.reserve(256);
  for (i32 z = 0; z < 16; ++z) {
    for (i32 y = 0; y < 16; ++y) {
      for (i32 x = 0; x < 16; ++x) {
        if (!occupied(grid, x, y, z)) {
          continue;
        }
        for (const voxel::BlockFace face : voxel::AllBlockFaces) {
          if (exposed_face(grid, x, y, z, face)) {
            add_exposed_face_edges(grid, edge_segments, x, y, z, face);
          }
        }
      }
    }
  }
  for (const EdgeSegment& edge : merged_edges(std::move(edge_segments))) {
    append_edge_box(asset, edge, thickness, inflate, uvs, input.color, input.atlas_page);
  }
  return asset;
}

} // namespace arv::render
