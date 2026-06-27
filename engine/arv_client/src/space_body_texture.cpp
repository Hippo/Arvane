#include "arv/client/space_body_texture.hpp"

#include "arv/core/coordinates.hpp"
#include "arv/world/worldgen/chunk_generator.hpp"
#include "arv/world/worldgen/loader.hpp"
#include "arv/world/worldgen/seeds.hpp"
#include "arv/world/worldgen/terrain_query.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>

namespace arv::client {
namespace {

struct RgbColor {
  f32 r{};
  f32 g{};
  f32 b{};
};

[[nodiscard]] u8 hex_digit(char c) noexcept {
  if (c >= '0' && c <= '9') {
    return static_cast<u8>(c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<u8>(10 + c - 'a');
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<u8>(10 + c - 'A');
  }
  return 0U;
}

[[nodiscard]] std::optional<RgbColor> parse_hex_color(std::string_view text) noexcept {
  if (text.size() != 7U || text.front() != '#') {
    return std::nullopt;
  }
  const auto channel = [&](usize offset) {
    return static_cast<f32>((hex_digit(text[offset]) << 4U) | hex_digit(text[offset + 1U])) /
           255.0F;
  };
  return RgbColor{channel(1U), channel(3U), channel(5U)};
}

[[nodiscard]] RgbColor color_from_value(
    const std::optional<world::worldgen::ColorValue>& color,
    RgbColor fallback) noexcept {
  if (!color.has_value()) {
    return fallback;
  }
  return parse_hex_color(color->text).value_or(fallback);
}

[[nodiscard]] RgbColor blend(RgbColor a, RgbColor b, f32 amount) noexcept {
  const f32 t = std::clamp(amount, 0.0F, 1.0F);
  return {
      a.r + (b.r - a.r) * t,
      a.g + (b.g - a.g) * t,
      a.b + (b.b - a.b) * t};
}

[[nodiscard]] RgbColor multiply(RgbColor color, f32 amount) noexcept {
  return {
      std::clamp(color.r * amount, 0.0F, 1.0F),
      std::clamp(color.g * amount, 0.0F, 1.0F),
      std::clamp(color.b * amount, 0.0F, 1.0F)};
}

void write_pixel(asset::Image& image, u32 x, u32 y, RgbColor color) {
  const usize offset =
      (static_cast<usize>(y) * static_cast<usize>(image.width) + static_cast<usize>(x)) *
      4U;
  image.pixels[offset + 0U] =
      static_cast<u8>(std::clamp(color.r, 0.0F, 1.0F) * 255.0F + 0.5F);
  image.pixels[offset + 1U] =
      static_cast<u8>(std::clamp(color.g, 0.0F, 1.0F) * 255.0F + 0.5F);
  image.pixels[offset + 2U] =
      static_cast<u8>(std::clamp(color.b, 0.0F, 1.0F) * 255.0F + 0.5F);
  image.pixels[offset + 3U] = 255U;
}

[[nodiscard]] bool contains_text(std::string_view value, std::string_view needle) noexcept {
  return value.find(needle) != std::string_view::npos;
}

[[nodiscard]] std::string surface_texture_asset(
    const voxel::BlockVariantRecord& variant) {
  const std::string& top =
      variant.texture_assets[voxel::face_index(voxel::BlockFace::PositiveY)];
  if (!top.empty()) {
    return top;
  }
  if (!variant.particle_texture_asset.empty()) {
    return variant.particle_texture_asset;
  }
  for (const std::string& texture : variant.texture_assets) {
    if (!texture.empty()) {
      return texture;
    }
  }
  return "arvane:texture/missing";
}

[[nodiscard]] RgbColor average_texture_color(
    asset::ResourceManager& resources,
    std::string_view texture_asset,
    HashMap<std::string, RgbColor>& cache) {
  if (texture_asset.empty()) {
    return {0.45F, 0.45F, 0.45F};
  }
  if (const auto found = cache.find(std::string{texture_asset}); found != cache.end()) {
    return found->second;
  }

  auto loaded = resources.load_texture({std::string{texture_asset}});
  if (!loaded) {
    return {0.45F, 0.45F, 0.45F};
  }
  const asset::TextureResource* resource = resources.texture(loaded.value());
  if (resource == nullptr || resource->image.empty()) {
    return {0.45F, 0.45F, 0.45F};
  }

  u64 r = 0U;
  u64 g = 0U;
  u64 b = 0U;
  u64 count = 0U;
  const asset::Image& image = resource->image;
  for (u32 y = 0; y < image.height; ++y) {
    for (u32 x = 0; x < image.width; ++x) {
      const usize offset =
          (static_cast<usize>(y) * static_cast<usize>(image.width) + static_cast<usize>(x)) *
          static_cast<usize>(image.channels);
      if (image.channels >= 4U && image.pixels[offset + 3U] < 16U) {
        continue;
      }
      r += image.pixels[offset + 0U];
      g += image.pixels[offset + 1U];
      b += image.pixels[offset + 2U];
      ++count;
    }
  }
  if (count == 0U) {
    return {0.45F, 0.45F, 0.45F};
  }

  const RgbColor result{
      static_cast<f32>(r) / static_cast<f32>(count) / 255.0F,
      static_cast<f32>(g) / static_cast<f32>(count) / 255.0F,
      static_cast<f32>(b) / static_cast<f32>(count) / 255.0F};
  cache.emplace(std::string{texture_asset}, result);
  return result;
}

[[nodiscard]] const world::worldgen::TerrainProfile& terrain_profile_for(
    const world::worldgen::ResolvedDimension& dimension,
    u64 seed,
    ChunkCoord coord,
    HashMap<ChunkCoord, world::worldgen::TerrainProfile>& cache) {
  if (const auto found = cache.find(coord); found != cache.end()) {
    return found->second;
  }
  auto [inserted, _] = cache.emplace(
      coord,
      world::worldgen::generate_terrain_profile(dimension, seed, coord));
  return inserted->second;
}

[[nodiscard]] RgbColor color_for_surface_sample(
    asset::ResourceManager& resources,
    const voxel::BlockRegistry& block_registry,
    const world::worldgen::ResolvedDimension& dimension,
    const world::worldgen::TerrainProfile& profile,
    usize column_index,
    BlockPos surface,
    HashMap<std::string, RgbColor>& texture_color_cache) {
  const usize biome_index = std::min(
      profile.biome_by_column[column_index],
      dimension.biomes.empty() ? 0U : dimension.biomes.size() - 1U);
  const world::worldgen::ResolvedBiome* biome =
      biome_index < dimension.biomes.size() ? &dimension.biomes[biome_index] : nullptr;
  const i32 sea_level = dimension.raw.vertical.sea_level;
  const bool has_water = dimension.water_block != voxel::AirBlockVariantID;
  const bool underwater = has_water && surface.value.y < sea_level;
  const bool ocean_biome = biome != nullptr &&
      world::worldgen::biome_has_tag(biome->raw, "arvane:biome/ocean");

  if (underwater || ocean_biome) {
    const RgbColor fallback_water{0.20F, 0.42F, 0.78F};
    return color_from_value(
        biome == nullptr ? std::optional<world::worldgen::ColorValue>{} : biome->raw.colors.water,
        fallback_water);
  }

  const voxel::BlockVariantID surface_block =
      world::worldgen::terrain_block_at(dimension, profile, surface);
  const voxel::BlockVariantRecord* variant = block_registry.variant(surface_block);
  RgbColor color{0.45F, 0.45F, 0.45F};
  if (variant != nullptr) {
    color = average_texture_color(
        resources,
        surface_texture_asset(*variant),
        texture_color_cache);
  }

  if (biome != nullptr) {
    const RgbColor biome_land =
        color_from_value(biome->raw.colors.grass, color_from_value(biome->raw.colors.fog, color));
    const bool lunar = world::worldgen::biome_has_tag(biome->raw, "arvane:biome/lunar");
    const bool grassy =
        variant != nullptr &&
        (contains_text(variant->debug_name, "grass") ||
         contains_text(surface_texture_asset(*variant), "grass"));
    color = blend(color, biome_land, lunar ? 0.82F : grassy ? 0.62F : 0.28F);
  }

  const f32 height_range = static_cast<f32>(
      std::max(1, dimension.raw.vertical.max_y - dimension.raw.vertical.min_y));
  const f32 height_t =
      static_cast<f32>(surface.value.y - dimension.raw.vertical.min_y) / height_range;
  const f32 slope = std::clamp(profile.slope_by_column[column_index] * 0.035F, 0.0F, 0.16F);
  const f32 shade = 0.88F + std::clamp(height_t, 0.0F, 1.0F) * 0.18F - slope;
  return multiply(color, shade);
}

[[nodiscard]] asset::Image generate_space_body_texture(
    asset::ResourceManager& resources,
    const voxel::BlockRegistry& block_registry,
    const world::worldgen::ResolvedDimension& dimension,
    u64 seed,
    SpaceBodyTextureBuildOptions options) {
  options.width = std::max(options.width, 16U);
  options.height = std::max(options.height, 8U);
  options.sample_span_blocks = std::max(options.sample_span_blocks, ChunkSize * 4);

  asset::Image image;
  image.width = options.width;
  image.height = options.height;
  image.channels = 4;
  image.pixels.resize(static_cast<usize>(image.width) * static_cast<usize>(image.height) * 4U);

  HashMap<ChunkCoord, world::worldgen::TerrainProfile> profile_cache;
  HashMap<std::string, RgbColor> texture_color_cache;
  profile_cache.reserve(static_cast<usize>(image.width) * static_cast<usize>(image.height) / 4U);

  for (u32 y = 0; y < image.height; ++y) {
    const f32 v = (static_cast<f32>(y) + 0.5F) / static_cast<f32>(image.height);
    for (u32 x = 0; x < image.width; ++x) {
      const f32 u = (static_cast<f32>(x) + 0.5F) / static_cast<f32>(image.width);
      const i32 world_x = static_cast<i32>(
          std::floor((u - 0.5F) * static_cast<f32>(options.sample_span_blocks)));
      const i32 world_z = static_cast<i32>(
          std::floor((v - 0.5F) * static_cast<f32>(options.sample_span_blocks)));
      const ChunkCoord coord{
          floor_div(world_x, ChunkSize),
          floor_div(world_z, ChunkSize)};
      const i32 local_x = floor_mod(world_x, ChunkSize);
      const i32 local_z = floor_mod(world_z, ChunkSize);
      const world::worldgen::TerrainProfile& profile =
          terrain_profile_for(dimension, seed, coord, profile_cache);
      const usize column_index = voxel::local_column_index({local_x, local_z});
      const BlockPos surface{{
          world_x,
          profile.surface_y_by_column[column_index],
          world_z}};
      const RgbColor color =
          color_for_surface_sample(
              resources,
              block_registry,
              dimension,
              profile,
              column_index,
              surface,
              texture_color_cache);
      write_pixel(image, x, y, color);
    }
  }
  return image;
}

[[nodiscard]] std::vector<const world::SpaceBodyDefinition*> sorted_space_bodies(
    const world::SpaceDefinitionRegistry& registry) {
  std::vector<const world::SpaceBodyDefinition*> bodies;
  bodies.reserve(registry.space_bodies.size());
  for (const auto& [_, body] : registry.space_bodies) {
    bodies.push_back(&body);
  }
  std::sort(
      bodies.begin(),
      bodies.end(),
      [](const world::SpaceBodyDefinition* lhs, const world::SpaceBodyDefinition* rhs) {
        return lhs->id < rhs->id;
      });
  return bodies;
}

} // namespace

SpaceBodyTextureAssets build_space_body_texture_assets(
    asset::ResourceManager& resources,
    const world::SpaceDefinitionRegistry& space_definitions,
    const world::worldgen::WorldGenRegistry& worldgen_registry,
    const voxel::BlockRegistry& block_registry,
    SpaceBodyTextureBuildOptions options) {
  SpaceBodyTextureAssets output;
  HashSet<std::string> seen_textures;

  for (const world::SpaceBodyDefinition* body : sorted_space_bodies(space_definitions)) {
    if (body == nullptr || body->render.texture.empty() ||
        !seen_textures.insert(body->render.texture).second) {
      continue;
    }
    output.texture_assets.push_back(body->render.texture);

    const world::WorldDefinition* target_world =
        space_definitions.find_world(body->target_world);
    if (target_world == nullptr || target_world->worldgen.empty()) {
      continue;
    }
    auto resolved = world::worldgen::resolve_dimension(
        worldgen_registry,
        target_world->worldgen,
        block_registry);
    if (!resolved) {
      continue;
    }
    const u64 seed =
        world::worldgen::mix64(
            world::worldgen::fnv1a(body->id) ^
            world::worldgen::fnv1a(target_world->worldgen));
    output.generated_textures.push_back({
        body->render.texture,
        generate_space_body_texture(
            resources,
            block_registry,
            *resolved.value(),
            seed,
            options)});
  }

  return output;
}

SpaceBodyTextureAssets load_space_body_texture_assets_from_packs(
    const asset::PackManager& packs,
    asset::ResourceManager& resources,
    const voxel::BlockRegistry& block_registry,
    SpaceBodyTextureBuildOptions options) {
  auto space_definitions = world::load_space_definition_registry_from_packs(packs);
  if (!space_definitions) {
    return {};
  }
  auto worldgen_registry = world::worldgen::load_worldgen_registry_from_packs(packs);
  if (!worldgen_registry) {
    return {};
  }
  return build_space_body_texture_assets(
      resources,
      space_definitions.value(),
      worldgen_registry.value(),
      block_registry,
      options);
}

} // namespace arv::client
