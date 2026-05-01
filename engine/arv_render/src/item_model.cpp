#include "arv/render/item_model.hpp"

#include <algorithm>

namespace arv::render {
namespace {

struct Quad {
  std::array<Vec3f, 4> positions{};
  std::array<Vec2f, 4> uvs{};
  Vec3f color{1.0F, 1.0F, 1.0F};
};

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
    asset.vertices.push_back({quad.positions[index], quad.uvs[index], quad.color, 0U});
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
       face_color(voxel::BlockFace::PositiveZ)});
  append_quad(
      asset,
      Quad{{{{x1, y0, z0}, {x0, y0, z0}, {x1, y1, z0}, {x0, y1, z0}}},
       std::array<Vec2f, 4>{{uvs[1], uvs[0], uvs[3], uvs[2]}},
       face_color(voxel::BlockFace::NegativeZ)});

  if (!visible_pixel(input, static_cast<i32>(x) - 1, static_cast<i32>(y))) {
    append_quad(
        asset,
        Quad{{{{x0, y0, z0}, {x0, y0, z1}, {x0, y1, z0}, {x0, y1, z1}}},
         std::array<Vec2f, 4>{{uvs[0], uvs[1], uvs[2], uvs[3]}},
         face_color(voxel::BlockFace::NegativeX)});
  }
  if (!visible_pixel(input, static_cast<i32>(x) + 1, static_cast<i32>(y))) {
    append_quad(
        asset,
        Quad{{{{x1, y0, z1}, {x1, y0, z0}, {x1, y1, z1}, {x1, y1, z0}}},
         std::array<Vec2f, 4>{{uvs[1], uvs[0], uvs[3], uvs[2]}},
         face_color(voxel::BlockFace::PositiveX)});
  }
  if (!visible_pixel(input, static_cast<i32>(x), static_cast<i32>(y) - 1)) {
    append_quad(
        asset,
        Quad{{{{x0, y1, z1}, {x1, y1, z1}, {x0, y1, z0}, {x1, y1, z0}}},
         std::array<Vec2f, 4>{{uvs[2], uvs[3], uvs[0], uvs[1]}},
         face_color(voxel::BlockFace::PositiveY)});
  }
  if (!visible_pixel(input, static_cast<i32>(x), static_cast<i32>(y) + 1)) {
    append_quad(
        asset,
        Quad{{{{x0, y0, z0}, {x1, y0, z0}, {x0, y0, z1}, {x1, y0, z1}}},
         std::array<Vec2f, 4>{{uvs[0], uvs[1], uvs[2], uvs[3]}},
         face_color(voxel::BlockFace::NegativeY)});
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

[[nodiscard]] std::array<Vec2f, 4> to_vec2f(std::array<Vec2i, 4> uvs) noexcept {
  std::array<Vec2f, 4> result{};
  for (usize index = 0; index < uvs.size(); ++index) {
    result[index] = {static_cast<f32>(uvs[index].x), static_cast<f32>(uvs[index].y)};
  }
  return result;
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

  for (const CubeFaceGeometry& face : CubeFaces) {
    const usize index = voxel::face_index(face.face);
    if (!input.face_valid[index]) {
      continue;
    }
    append_quad(
        asset,
        {face.corners, to_vec2f(input.face_uvs[index]), face_color(face.face)});
  }

  return asset;
}

} // namespace arv::render
