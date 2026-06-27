#include "arv/world/world_generation.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/world/worldgen/chunk_generator.hpp"
#include "arv/world/worldgen/terrain_query.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <utility>

#include <FastNoise/FastNoise.h>

namespace arv::world {
namespace {

[[nodiscard]] constexpr u64 fnv1a(std::string_view text) noexcept {
  u64 value = 14695981039346656037ULL;
  for (const char ch : text) {
    value ^= static_cast<u8>(ch);
    value *= 1099511628211ULL;
  }
  return value;
}

[[nodiscard]] constexpr u64 mix64(u64 value) noexcept {
  value += 0x9E3779B97F4A7C15ULL;
  value = (value ^ (value >> 30U)) * 0xBF58476D1CE4E5B9ULL;
  value = (value ^ (value >> 27U)) * 0x94D049BB133111EBULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] constexpr u64 generator_seed(u64 seed, u64 salt, u32 version) noexcept {
  u64 value = seed ^ mix64(salt) ^ (static_cast<u64>(version) << 32U);
  return mix64(value);
}

[[nodiscard]] bool id_has_leaf(std::string_view id, std::string_view leaf) noexcept {
  if (id.size() < leaf.size()) {
    return false;
  }
  if (id.substr(id.size() - leaf.size()) != leaf) {
    return false;
  }
  return id.size() == leaf.size() || id[id.size() - leaf.size() - 1U] == '/';
}

[[nodiscard]] bool disallowed_spawn_biome(
    const worldgen::ResolvedBiome& biome) noexcept {
  return worldgen::biome_has_tag(biome.raw, "arvane:biome/ocean") ||
         worldgen::biome_has_tag(biome.raw, "arvane:biome/desert") ||
         id_has_leaf(biome.raw.id, "ocean") ||
         id_has_leaf(biome.raw.id, "deep_ocean") ||
         id_has_leaf(biome.raw.id, "desert");
}

[[nodiscard]] Vec2i spawn_candidate_column(u64 seed, u32 attempt) noexcept {
  if (attempt == 0U) {
    return {};
  }
  const u64 mixed =
      mix64(seed ^ fnv1a("arvane:world_spawn") ^ (static_cast<u64>(attempt) * 0x9E3779B97F4A7C15ULL));
  const f32 unit_angle =
      static_cast<f32>(mixed & 0xFFFFFFULL) / static_cast<f32>(0x1000000ULL);
  const f32 angle = unit_angle * TauRadians;
  const f32 radius = 8.0F + std::sqrt(static_cast<f32>(attempt)) * 14.0F;
  return {
      static_cast<i32>(std::round(std::cos(angle) * radius)),
      static_cast<i32>(std::round(std::sin(angle) * radius))};
}

[[nodiscard]] std::optional<arv::Location> resolved_spawn_candidate(
    const worldgen::ResolvedDimension& dimension,
    u64 seed,
    i32 world_x,
    i32 world_z) noexcept {
  if (dimension.biomes.empty()) {
    return std::nullopt;
  }

  const ChunkCoord coord{floor_div(world_x, ChunkSize), floor_div(world_z, ChunkSize)};
  const worldgen::TerrainProfile profile =
      worldgen::generate_terrain_profile(dimension, seed, coord);
  const usize column_index = voxel::local_column_index(
      {floor_mod(world_x, ChunkSize), floor_mod(world_z, ChunkSize)});
  const usize biome_index =
      std::min(profile.biome_by_column[column_index], dimension.biomes.size() - 1U);
  if (disallowed_spawn_biome(dimension.biomes[biome_index])) {
    return std::nullopt;
  }

  const i32 surface_y = profile.surface_y_by_column[column_index];
  if (surface_y < dimension.raw.vertical.sea_level) {
    return std::nullopt;
  }

  const BlockPos ground{{world_x, surface_y, world_z}};
  const BlockPos feet{{world_x, surface_y + 1, world_z}};
  const BlockPos head{{world_x, surface_y + 2, world_z}};
  if (worldgen::terrain_block_at(dimension, profile, ground) == voxel::AirBlockVariantID ||
      worldgen::terrain_block_at(dimension, profile, feet) != voxel::AirBlockVariantID ||
      worldgen::terrain_block_at(dimension, profile, head) != voxel::AirBlockVariantID) {
    return std::nullopt;
  }

  return arv::Location{
      static_cast<f32>(world_x) + 0.5F,
      static_cast<f32>(surface_y) + 1.0F,
      static_cast<f32>(world_z) + 0.5F,
      0.0F,
      0.0F};
}

[[nodiscard]] std::optional<arv::Location> legacy_spawn_candidate(
    const WorldGenerationConfig& config,
    u64 seed,
    i32 world_x,
    i32 world_z) noexcept {
  const i32 surface_y = terrain_height_at(config, seed, world_x, world_z);
  if (surface_y < config.sea_level) {
    return std::nullopt;
  }
  return arv::Location{
      static_cast<f32>(world_x) + 0.5F,
      static_cast<f32>(surface_y) + 1.0F,
      static_cast<f32>(world_z) + 0.5F,
      0.0F,
      0.0F};
}

class TerrainNoiseSampler {
public:
  static constexpr i32 UpsampleStep = 4;

  TerrainNoiseSampler(const WorldGenerationConfig& config, u64 seed) noexcept
      : config_(config),
        salt_(fnv1a(config.seed_salt)),
        mixed_seed_(generator_seed(seed, salt_, config.version)) {
    seed_bias_ =
        (static_cast<f32>((mixed_seed_ >> 48U) & 0xFFFFU) / static_cast<f32>(0xFFFFU) * 2.0F - 1.0F) *
        0.08F;

    auto simplex = FastNoise::New<FastNoise::Simplex>(FastSIMD::FeatureSet::Max);
    if (!simplex) {
      return;
    }

    generator_ = simplex;
    if (config.terrain_octaves > 1U) {
      auto fractal = FastNoise::New<FastNoise::FractalFBm>(FastSIMD::FeatureSet::Max);
      if (fractal) {
        fractal->SetSource(simplex);
        fractal->SetOctaveCount(static_cast<int>(config.terrain_octaves));
        fractal->SetLacunarity(config.terrain_lacunarity);
        fractal->SetGain(config.terrain_gain);
        generator_ = fractal;
      }
    }

    layers_[0] = make_layer(0.35F, 0xD1B54A32D192ED03ULL);
    layers_[1] = make_layer(1.0F, 0xABC98388FB8FAC03ULL);
    layers_[2] = make_layer(3.75F, 0x8CB92BA72F3D8DD7ULL);
  }

  [[nodiscard]] f32 terrain(i32 world_x, i32 world_z) const noexcept {
    return combine_layers(
        sample_layer(world_x, world_z, layers_[0]),
        sample_layer(world_x, world_z, layers_[1]),
        sample_layer(world_x, world_z, layers_[2]));
  }

  [[nodiscard]] f32 interpolated_terrain(i32 world_x, i32 world_z) const noexcept {
    const i32 base_x = floor_div(world_x, UpsampleStep) * UpsampleStep;
    const i32 base_z = floor_div(world_z, UpsampleStep) * UpsampleStep;
    const f32 t_x = static_cast<f32>(world_x - base_x) / static_cast<f32>(UpsampleStep);
    const f32 t_z = static_cast<f32>(world_z - base_z) / static_cast<f32>(UpsampleStep);
    return interpolate(
        terrain(base_x, base_z),
        terrain(base_x + UpsampleStep, base_z),
        terrain(base_x, base_z + UpsampleStep),
        terrain(base_x + UpsampleStep, base_z + UpsampleStep),
        t_x,
        t_z);
  }

  [[nodiscard]] i32 height_from_terrain(f32 terrain) const noexcept {
    const f32 height = static_cast<f32>(config_.base_height) +
                       terrain * static_cast<f32>(config_.height_scale);
    const i32 rounded = static_cast<i32>(std::floor(height + 0.5F));
    return std::clamp(rounded, config_.min_y, config_.max_y);
  }

  [[nodiscard]] static f32 lerp(f32 a, f32 b, f32 t) noexcept {
    return a + (b - a) * t;
  }

  [[nodiscard]] static f32 interpolate(
      f32 lower_left,
      f32 lower_right,
      f32 upper_left,
      f32 upper_right,
      f32 t_x,
      f32 t_z) noexcept {
    return lerp(lerp(lower_left, lower_right, t_x), lerp(upper_left, upper_right, t_x), t_z);
  }

private:
  struct NoiseLayer {
    f32 frequency{};
    f32 offset_x{};
    f32 offset_z{};
    int seed{};
  };

  [[nodiscard]] NoiseLayer make_layer(f32 frequency_scale, u64 layer_salt) const noexcept {
    const u64 layer_seed = mix64(mixed_seed_ ^ mix64(salt_ ^ layer_salt));
    return {
        config_.terrain_frequency * frequency_scale,
        static_cast<f32>((layer_seed >> 8U) & 0xFFFFU) * 0.125F,
        static_cast<f32>((layer_seed >> 24U) & 0xFFFFU) * 0.125F,
        static_cast<int>(layer_seed & 0x7FFFFFFFULL)};
  }

  [[nodiscard]] f32 sample_layer(i32 world_x, i32 world_z, const NoiseLayer& layer) const noexcept {
    if (!generator_) {
      return 0.0F;
    }

    return generator_->GenSingle2D(
        static_cast<f32>(world_x) * layer.frequency + layer.offset_x,
        static_cast<f32>(world_z) * layer.frequency + layer.offset_z,
        layer.seed);
  }

  [[nodiscard]] f32 combine_layers(f32 continental, f32 hills, f32 detail) const noexcept {
    return ((continental * 0.55F) + (hills * 0.35F) + (detail * 0.10F)) *
               config_.terrain_amplitude +
           seed_bias_;
  }

  const WorldGenerationConfig& config_;
  u64 salt_{};
  u64 mixed_seed_{};
  std::array<NoiseLayer, 3> layers_{};
  f32 seed_bias_{};
  FastNoise::SmartNode<> generator_{};
};

class ChunkTerrainHeightCache {
public:
  ChunkTerrainHeightCache(const WorldGenerationConfig& config, u64 seed, ChunkCoord coord) noexcept
      : sampler_(config, seed),
        origin_x_(coord.x * ChunkSize),
        origin_z_(coord.z * ChunkSize) {
    for (i32 grid_z = 0; grid_z < GridSide; ++grid_z) {
      for (i32 grid_x = 0; grid_x < GridSide; ++grid_x) {
        lattice_[grid_index(grid_x, grid_z)] = sampler_.terrain(
            origin_x_ + grid_x * TerrainNoiseSampler::UpsampleStep,
            origin_z_ + grid_z * TerrainNoiseSampler::UpsampleStep);
      }
    }
  }

  [[nodiscard]] i32 height_at(i32 local_x, i32 local_z) const noexcept {
    const i32 grid_x = local_x / TerrainNoiseSampler::UpsampleStep;
    const i32 grid_z = local_z / TerrainNoiseSampler::UpsampleStep;
    const f32 t_x = static_cast<f32>(local_x % TerrainNoiseSampler::UpsampleStep) /
                    static_cast<f32>(TerrainNoiseSampler::UpsampleStep);
    const f32 t_z = static_cast<f32>(local_z % TerrainNoiseSampler::UpsampleStep) /
                    static_cast<f32>(TerrainNoiseSampler::UpsampleStep);
    return sampler_.height_from_terrain(TerrainNoiseSampler::interpolate(
        lattice_[grid_index(grid_x, grid_z)],
        lattice_[grid_index(grid_x + 1, grid_z)],
        lattice_[grid_index(grid_x, grid_z + 1)],
        lattice_[grid_index(grid_x + 1, grid_z + 1)],
        t_x,
        t_z));
  }

private:
  static constexpr i32 GridSide = (ChunkSize / TerrainNoiseSampler::UpsampleStep) + 1;
  static_assert(ChunkSize % TerrainNoiseSampler::UpsampleStep == 0);

  [[nodiscard]] static constexpr usize grid_index(i32 grid_x, i32 grid_z) noexcept {
    return static_cast<usize>(grid_x) + static_cast<usize>(GridSide) * static_cast<usize>(grid_z);
  }

  TerrainNoiseSampler sampler_;
  i32 origin_x_{};
  i32 origin_z_{};
  std::array<f32, static_cast<usize>(GridSide* GridSide)> lattice_{};
};

[[nodiscard]] const io::DataValue* member(
    const io::DataValue& parent,
    std::string_view key) noexcept {
  return parent.get(key);
}

void read_i32(const io::DataValue& parent, const char* key, i32& value) {
  const io::DataValue* child = member(parent, key);
  if (child != nullptr) {
    value = static_cast<i32>(child->as_i64_or(value));
  }
}

void read_u32(const io::DataValue& parent, const char* key, u32& value) {
  const io::DataValue* child = member(parent, key);
  if (child != nullptr) {
    value = static_cast<u32>(child->as_u64_or(value));
  }
}

void read_f32(const io::DataValue& parent, const char* key, f32& value) {
  const io::DataValue* child = member(parent, key);
  if (child != nullptr) {
    value = static_cast<f32>(child->as_f64_or(value));
  }
}

} // namespace

WorldGenerationConfig config_from_resolved_dimension(
    std::shared_ptr<const worldgen::ResolvedDimension> dimension) {
  WorldGenerationConfig config;
  if (dimension == nullptr) {
    return config;
  }
  config.id = dimension->raw.id;
  config.dimension_id = dimension->raw.id;
  config.version = dimension->raw.version;
  config.seed_salt = dimension->raw.seed_salt;
  config.min_y = dimension->raw.vertical.min_y;
  config.max_y = dimension->raw.vertical.max_y;
  config.sea_level = dimension->raw.vertical.sea_level;
  config.base_height = dimension->terrain.height.base;
  config.height_scale = dimension->terrain.height.scale;
  config.terrain_noise_type = dimension->terrain.primary_noise.type_id;
  config.terrain_frequency = dimension->terrain.primary_noise.frequency;
  config.terrain_octaves = dimension->terrain.primary_noise.octaves;
  config.terrain_amplitude = dimension->terrain.primary_noise.amplitude;
  config.terrain_lacunarity = dimension->terrain.primary_noise.lacunarity;
  config.terrain_gain = dimension->terrain.primary_noise.gain;
  config.resolved_dimension = std::move(dimension);
  return config;
}

WorldGenerationConfig make_void_world_generation_config() {
  WorldGenerationConfig config;
  config.id = std::string{VoidWorldGenerationID};
  config.dimension_id = std::string{VoidWorldGenerationID};
  config.version = 1U;
  config.seed_salt = "void";
  config.min_y = -64;
  config.max_y = 128;
  config.sea_level = 0;
  config.base_height = config.min_y - 1;
  config.height_scale = 0;
  config.terrain_noise_type = "void";
  config.terrain_frequency = 1.0F;
  config.terrain_octaves = 1U;
  config.terrain_amplitude = 1.0F;
  config.terrain_lacunarity = 1.0F;
  config.terrain_gain = 1.0F;
  config.void_world = true;
  return config;
}

WorldGenerationConfig refresh_generation_config_for_current_dimension(
    WorldGenerationConfig saved_config,
    const WorldGenerationConfig& current_default_config) {
  if (saved_config.void_world ||
      saved_config.dimension_id == VoidWorldGenerationID ||
      saved_config.id == VoidWorldGenerationID) {
    saved_config.void_world = true;
    saved_config.resolved_dimension.reset();
    return saved_config;
  }
  const bool same_dimension =
      saved_config.dimension_id.empty() ||
      saved_config.dimension_id == current_default_config.dimension_id;
  saved_config.resolved_dimension = current_default_config.resolved_dimension;
  if (!same_dimension) {
    return saved_config;
  }

  const i32 min_y = std::min(saved_config.min_y, current_default_config.min_y);
  const i32 max_y = std::max(saved_config.max_y, current_default_config.max_y);
  WorldGenerationConfig refreshed = current_default_config;
  refreshed.min_y = min_y;
  refreshed.max_y = max_y;
  return refreshed;
}

WorldGenerationConfigResult parse_world_generation_yaml(std::string_view text) {
  WorldGenerationConfig config;

  auto decoded = io::decode_yaml_value(text);
  if (!decoded) {
    return WorldGenerationConfigResult::failure(
        {"failed to parse world generation YAML: " + decoded.error().describe()});
  }
  const io::DataValue& root = decoded.value().root;
  if (root.kind() == io::DataKind::Null) {
    return WorldGenerationConfigResult::failure({"world generation YAML is empty"});
  }

  if (const io::DataValue* id = member(root, "id"); id != nullptr) {
    config.id = std::string{id->as_string_or(config.id)};
  }
  read_u32(root, "version", config.version);
  if (const io::DataValue* seed_salt = member(root, "seed_salt");
      seed_salt != nullptr) {
    config.seed_salt = std::string{seed_salt->as_string_or(config.seed_salt)};
  }
  if (const io::DataValue* void_world = member(root, "void");
      void_world != nullptr && void_world->kind() == io::DataKind::Bool) {
    config.void_world = void_world->as_bool_or(config.void_world);
  }
  if (const io::DataValue* void_world = member(root, "void_world");
      void_world != nullptr && void_world->kind() == io::DataKind::Bool) {
    config.void_world = void_world->as_bool_or(config.void_world);
  }

  if (const io::DataValue* vertical = member(root, "vertical"); vertical != nullptr) {
    read_i32(*vertical, "min_y", config.min_y);
    read_i32(*vertical, "max_y", config.max_y);
    read_i32(*vertical, "sea_level", config.sea_level);
  }

  if (const io::DataValue* density = member(root, "density"); density != nullptr) {
    read_i32(*density, "base_height", config.base_height);
    read_i32(*density, "height_scale", config.height_scale);
  }

  const io::DataValue* noise = member(root, "noise");
  const io::DataValue* terrain_noise = noise == nullptr ? nullptr : member(*noise, "terrain");
  if (terrain_noise != nullptr) {
    if (const io::DataValue* type = member(*terrain_noise, "type"); type != nullptr) {
      config.terrain_noise_type = std::string{type->as_string_or(config.terrain_noise_type)};
    }
    read_f32(*terrain_noise, "frequency", config.terrain_frequency);
    read_u32(*terrain_noise, "octaves", config.terrain_octaves);
    read_f32(*terrain_noise, "amplitude", config.terrain_amplitude);
    read_f32(*terrain_noise, "lacunarity", config.terrain_lacunarity);
    read_f32(*terrain_noise, "gain", config.terrain_gain);
  }

  const io::DataValue* surface = member(root, "surface");
  const io::DataValue* rules = surface == nullptr ? nullptr : member(*surface, "rules");
  if (rules != nullptr) {
    const io::DataValue::Array* rule_array = rules->array_if();
    if (rule_array == nullptr || rule_array->empty()) {
      return WorldGenerationConfigResult::failure({"surface.rules must be a non-empty sequence"});
    }

    const io::DataValue* layers = member((*rule_array)[0], "layers");
    const io::DataValue::Array* layer_array =
        layers == nullptr ? nullptr : layers->array_if();
    if (layer_array == nullptr || layer_array->empty()) {
      return WorldGenerationConfigResult::failure({"surface rule must contain a non-empty layers sequence"});
    }

    for (const io::DataValue& layer_node : *layer_array) {
      SurfaceLayerDefinition layer;
      const io::DataValue* block = member(layer_node, "block");
      const io::DataValue* depth = member(layer_node, "depth");
      if (block == nullptr || depth == nullptr) {
        return WorldGenerationConfigResult::failure({"surface layer must contain block and depth"});
      }
      layer.block_name = std::string{block->as_string_or({})};
      if (depth->kind() == io::DataKind::String &&
          depth->as_string_or({}) == "remaining") {
        layer.remaining = true;
      } else {
        layer.depth = static_cast<i32>(depth->as_i64_or(0));
        if (layer.depth <= 0) {
          return WorldGenerationConfigResult::failure({"surface layer depth must be positive"});
        }
      }
      config.surface_layers.emplace_back(std::move(layer));
    }
  }

  if (config.id.empty()) {
    return WorldGenerationConfigResult::failure({"world generation id must not be empty"});
  }
  if (config.version == 0U) {
    return WorldGenerationConfigResult::failure({"world generation version must be non-zero"});
  }
  if (config.min_y > config.max_y) {
    return WorldGenerationConfigResult::failure({"vertical.min_y must be <= vertical.max_y"});
  }
  if (config.void_world || config.id == VoidWorldGenerationID ||
      config.dimension_id == VoidWorldGenerationID) {
    config.void_world = true;
    config.resolved_dimension.reset();
    return WorldGenerationConfigResult::success(std::move(config));
  }
  if (config.terrain_frequency <= 0.0F) {
    return WorldGenerationConfigResult::failure({"noise.terrain.frequency must be positive"});
  }
  if (config.terrain_octaves == 0U) {
    return WorldGenerationConfigResult::failure({"noise.terrain.octaves must be non-zero"});
  }
  if (config.terrain_amplitude <= 0.0F) {
    return WorldGenerationConfigResult::failure({"noise.terrain.amplitude must be positive"});
  }
  if (config.terrain_lacunarity <= 0.0F) {
    return WorldGenerationConfigResult::failure({"noise.terrain.lacunarity must be positive"});
  }
  if (config.terrain_gain <= 0.0F) {
    return WorldGenerationConfigResult::failure({"noise.terrain.gain must be positive"});
  }
  if (config.terrain_noise_type != "simplex_2d" &&
      config.terrain_noise_type != "open_simplex_2d") {
    return WorldGenerationConfigResult::failure(
        {"unsupported noise.terrain.type: " + config.terrain_noise_type});
  }
  if (config.surface_layers.empty()) {
    return WorldGenerationConfigResult::failure(
        {"surface.rules must define at least one surface layer"});
  }

  return WorldGenerationConfigResult::success(std::move(config));
}

WorldGenerationPaletteResult resolve_world_generation_palette(
    const WorldGenerationConfig& config,
    const voxel::BlockRegistry& registry) {
  WorldGenerationPalette palette;
  palette.resolved_dimension = config.resolved_dimension;
  if (config.void_world || config.dimension_id == VoidWorldGenerationID ||
      config.id == VoidWorldGenerationID) {
    return WorldGenerationPaletteResult::success(std::move(palette));
  }
  if (palette.resolved_dimension != nullptr && config.surface_layers.empty()) {
    return WorldGenerationPaletteResult::success(std::move(palette));
  }
  palette.surface_layers.reserve(config.surface_layers.size());

  bool has_remaining = false;
  for (const SurfaceLayerDefinition& layer : config.surface_layers) {
    const std::optional<voxel::BlockVariantID> block = registry.find_variant(layer.block_name);
    if (!block) {
      return WorldGenerationPaletteResult::failure(
          {"world generation references unknown block variant: " + layer.block_name});
    }
    has_remaining = has_remaining || layer.remaining;
    palette.surface_layers.emplace_back(block.value(), layer.depth, layer.remaining);
  }

  if (!has_remaining) {
    return WorldGenerationPaletteResult::failure({"surface layers must include a remaining layer"});
  }

  return WorldGenerationPaletteResult::success(std::move(palette));
}

i32 terrain_height_at(
    const WorldGenerationConfig& config,
    u64 seed,
    i32 world_x,
    i32 world_z) noexcept {
  if (config.void_world || config.dimension_id == VoidWorldGenerationID ||
      config.id == VoidWorldGenerationID) {
    return config.min_y - 1;
  }
  if (config.resolved_dimension != nullptr) {
    const ChunkCoord coord{floor_div(world_x, ChunkSize), floor_div(world_z, ChunkSize)};
    const worldgen::TerrainProfile profile =
        worldgen::generate_terrain_profile(*config.resolved_dimension, seed, coord);
    return profile.surface_y_by_column[voxel::local_column_index(
        {floor_mod(world_x, ChunkSize), floor_mod(world_z, ChunkSize)})];
  }
  const TerrainNoiseSampler sampler{config, seed};
  return sampler.height_from_terrain(sampler.interpolated_terrain(world_x, world_z));
}

GenerationSurfaceProfile terrain_height_profile_at(
    const WorldGenerationConfig& config,
    u64 seed,
    ChunkCoord coord) noexcept {
  if (config.void_world || config.dimension_id == VoidWorldGenerationID ||
      config.id == VoidWorldGenerationID) {
    GenerationSurfaceProfile surface_y_by_column{};
    surface_y_by_column.fill(config.min_y - 1);
    return surface_y_by_column;
  }
  if (config.resolved_dimension != nullptr) {
    return worldgen::generate_terrain_profile(*config.resolved_dimension, seed, coord)
        .surface_y_by_column;
  }
  const ChunkTerrainHeightCache height_cache{config, seed, coord};
  GenerationSurfaceProfile surface_y_by_column{};
  for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
    const ChunkLocalColumn column = voxel::local_column(column_index);
    surface_y_by_column[column_index] = height_cache.height_at(column.x, column.z);
  }
  return surface_y_by_column;
}

voxel::BlockVariantID choose_surface_block(
    const WorldGenerationPalette& palette,
    i32 depth_from_surface) noexcept {
  i32 consumed_depth = 0;
  for (const ResolvedSurfaceLayer& layer : palette.surface_layers) {
    if (layer.remaining) {
      return layer.block;
    }
    consumed_depth += layer.depth;
    if (depth_from_surface < consumed_depth) {
      return layer.block;
    }
  }
  return palette.surface_layers.empty() ? voxel::AirBlockVariantID : palette.surface_layers.back().block;
}

arv::Location generate_world_spawn_location(
    const WorldGenerationConfig& config,
    u64 seed) noexcept {
  if (config.void_world || config.dimension_id == VoidWorldGenerationID ||
      config.id == VoidWorldGenerationID) {
    return arv::Location{0.5F, 0.0F, 0.5F, 0.0F, 0.0F};
  }
  constexpr u32 MaxSpawnAttempts = 768U;
  for (u32 attempt = 0; attempt < MaxSpawnAttempts; ++attempt) {
    const Vec2i candidate = spawn_candidate_column(seed, attempt);
    std::optional<arv::Location> spawn =
        config.resolved_dimension != nullptr
            ? resolved_spawn_candidate(
                  *config.resolved_dimension,
                  seed,
                  candidate.x,
                  candidate.y)
            : legacy_spawn_candidate(config, seed, candidate.x, candidate.y);
    if (spawn.has_value()) {
      return spawn.value();
    }
  }

  const i32 surface_y = terrain_height_at(config, seed, 0, 0);
  return arv::Location{
      0.5F,
      static_cast<f32>(surface_y) + 1.0F,
      0.5F,
      0.0F,
      0.0F};
}

GeneratedChunkResult generate_chunk(const ChunkGenerationRequest& request) {
  ARV_PROFILE_ZONE("Chunk generation");
  std::array<char, core::PerfEventContextCapacity> profile_context{};
  if constexpr (core::perf_stats_enabled()) {
    (void)std::snprintf(
        profile_context.data(),
        profile_context.size(),
        "column=(%d,%d) y=%d..%d",
        request.coord.x,
        request.coord.z,
        request.min_chunk_y,
        request.max_chunk_y);
  }
  const core::ScopedThreadTiming timing{
      core::PerfMetric::GenerationTask,
      "worker/server",
      profile_context.data()};
  const std::shared_ptr<const worldgen::ResolvedDimension> resolved_dimension =
      request.resolved_dimension != nullptr
          ? request.resolved_dimension
          : (request.palette.resolved_dimension != nullptr
                 ? request.palette.resolved_dimension
                 : request.config.resolved_dimension);
  if (resolved_dimension != nullptr) {
    return worldgen::generate_resolved_chunk(request, *resolved_dimension);
  }

  if (request.config.void_world ||
      request.config.dimension_id == VoidWorldGenerationID ||
      request.config.id == VoidWorldGenerationID) {
    GeneratedChunk chunk;
    chunk.world_id = request.world_id;
    chunk.seed = request.seed;
    chunk.generator_version = request.config.version;
    chunk.coord = request.coord;
    chunk.surface_y_by_column.fill(request.config.min_y - 1);
    const i32 min_chunk_y = std::min(request.min_chunk_y, request.max_chunk_y);
    const i32 max_chunk_y = std::max(request.min_chunk_y, request.max_chunk_y);
    chunk.sections.reserve(static_cast<usize>(max_chunk_y - min_chunk_y + 1));
    for (i32 chunk_y = min_chunk_y; chunk_y <= max_chunk_y; ++chunk_y) {
      GeneratedChunkSection section;
      section.coord = chunk_section(request.coord, chunk_y);
      section.blocks.fill(voxel::AirBlockVariantID);
      chunk.sections.emplace_back(std::move(section));
    }
    return GeneratedChunkResult::success(std::move(chunk));
  }

  if (request.palette.surface_layers.empty()) {
    return GeneratedChunkResult::failure({"chunk generation request has no resolved surface layers"});
  }

  GeneratedChunk chunk;
  chunk.world_id = request.world_id;
  chunk.seed = request.seed;
  chunk.generator_version = request.config.version;
  chunk.coord = request.coord;
  const i32 min_chunk_y = std::min(request.min_chunk_y, request.max_chunk_y);
  const i32 max_chunk_y = std::max(request.min_chunk_y, request.max_chunk_y);
  chunk.sections.reserve(static_cast<usize>(max_chunk_y - min_chunk_y + 1));

  const GenerationSurfaceProfile surface_y_by_column =
      terrain_height_profile_at(request.config, request.seed, request.coord);
  chunk.surface_y_by_column = surface_y_by_column;

  i32 max_surface_y = std::numeric_limits<i32>::min();
  for (const i32 surface_y : surface_y_by_column) {
    max_surface_y = std::max(max_surface_y, surface_y);
  }

  for (i32 chunk_y = min_chunk_y; chunk_y <= max_chunk_y; ++chunk_y) {
    GeneratedChunkSection section;
    section.coord = chunk_section(request.coord, chunk_y);
    section.blocks.fill(voxel::AirBlockVariantID);

    const i32 section_min_y = chunk_y * ChunkSize;
    const i32 section_max_y = section_min_y + ChunkSize - 1;
    if (section_max_y < request.config.min_y ||
        section_min_y > request.config.max_y ||
        section_min_y > max_surface_y) {
      chunk.sections.emplace_back(std::move(section));
      continue;
    }

    const i32 fill_min_y = std::max(section_min_y, request.config.min_y);
    const i32 fill_max_y = std::min({section_max_y, request.config.max_y, max_surface_y});
    if (fill_max_y < fill_min_y) {
      chunk.sections.emplace_back(std::move(section));
      continue;
    }

    const i32 local_y_begin = fill_min_y - section_min_y;
    for (usize column_index = 0; column_index < voxel::ChunkArea; ++column_index) {
      const i32 surface_y = surface_y_by_column[column_index];
      if (fill_min_y > surface_y) {
        continue;
      }

      const ChunkLocalColumn column = voxel::local_column(column_index);
      const i32 local_y_end = std::min(fill_max_y, surface_y) - section_min_y;
      usize block_index = voxel::local_index({{column.x, local_y_begin, column.z}});
      for (i32 local_y = local_y_begin; local_y <= local_y_end; ++local_y) {
        const i32 world_y = section_min_y + local_y;
        const i32 depth_from_surface = surface_y - world_y;
        section.blocks[block_index] = choose_surface_block(request.palette, depth_from_surface);
        block_index += voxel::ChunkSide;
      }
    }
    chunk.sections.emplace_back(std::move(section));
  }

  return GeneratedChunkResult::success(std::move(chunk));
}

} // namespace arv::world
