#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/storage.hpp"

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <FastNoise/FastNoise.h>
#include <yaml-cpp/yaml.h>

namespace arv::world {

struct WorldGenerationError {
  std::string message{};
};

struct SurfaceLayerDefinition {
  std::string block_name{};
  i32 depth{};
  bool remaining{};
};

struct WorldGenerationConfig {
  std::string id{"arvane:overworld"};
  u32 version{1};
  std::string seed_salt{"overworld"};
  i32 min_y{-64};
  i32 max_y{128};
  i32 sea_level{32};
  i32 base_height{16};
  i32 height_scale{8};
  std::string terrain_noise_type{"simplex_2d"};
  f32 terrain_frequency{0.015F};
  u32 terrain_octaves{3};
  f32 terrain_amplitude{1.0F};
  f32 terrain_lacunarity{2.0F};
  f32 terrain_gain{0.5F};
  std::vector<SurfaceLayerDefinition> surface_layers{};
};

struct ResolvedSurfaceLayer {
  voxel::BlockVariantID block{};
  i32 depth{};
  bool remaining{};
};

struct WorldGenerationPalette {
  std::vector<ResolvedSurfaceLayer> surface_layers{};
};

struct ChunkGenerationRequest {
  WorldID world_id{};
  u64 seed{};
  ChunkCoord coord{};
  WorldGenerationConfig config{};
  WorldGenerationPalette palette{};
};

struct GeneratedChunk {
  WorldID world_id{};
  u64 seed{};
  u32 generator_version{};
  ChunkCoord coord{};
  std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks{};
};

using WorldGenerationConfigResult = Expected<WorldGenerationConfig, WorldGenerationError>;
using WorldGenerationPaletteResult = Expected<WorldGenerationPalette, WorldGenerationError>;
using GeneratedChunkResult = Expected<GeneratedChunk, WorldGenerationError>;

namespace detail {

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

class TerrainNoiseSampler {
public:
  TerrainNoiseSampler(const WorldGenerationConfig& config, u64 seed) noexcept
      : config_(config),
        salt_(fnv1a(config.seed_salt)),
        mixed_seed_(generator_seed(seed, salt_, config.version)) {
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
  }

  [[nodiscard]] f32 terrain(i32 world_x, i32 world_z) const noexcept {
    const f32 continental = sample_layer(world_x, world_z, 0.35F, 0xD1B54A32D192ED03ULL);
    const f32 hills = sample_layer(world_x, world_z, 1.0F, 0xABC98388FB8FAC03ULL);
    const f32 detail = sample_layer(world_x, world_z, 3.75F, 0x8CB92BA72F3D8DD7ULL);
    const f32 seed_bias =
        (static_cast<f32>((mixed_seed_ >> 48U) & 0xFFFFU) / static_cast<f32>(0xFFFFU) * 2.0F - 1.0F) *
        0.08F;
    return ((continental * 0.55F) + (hills * 0.35F) + (detail * 0.10F)) *
               config_.terrain_amplitude +
           seed_bias;
  }

private:
  [[nodiscard]] f32 sample_layer(
      i32 world_x,
      i32 world_z,
      f32 frequency_scale,
      u64 layer_salt) const noexcept {
    if (!generator_) {
      return 0.0F;
    }

    const u64 layer_seed = mix64(mixed_seed_ ^ mix64(salt_ ^ layer_salt));
    const f32 domain_offset_x = static_cast<f32>((layer_seed >> 8U) & 0xFFFFU) * 0.125F;
    const f32 domain_offset_z = static_cast<f32>((layer_seed >> 24U) & 0xFFFFU) * 0.125F;
    return generator_->GenSingle2D(
        static_cast<f32>(world_x) * config_.terrain_frequency * frequency_scale + domain_offset_x,
        static_cast<f32>(world_z) * config_.terrain_frequency * frequency_scale + domain_offset_z,
        static_cast<int>(layer_seed & 0x7FFFFFFFULL));
  }

  const WorldGenerationConfig& config_;
  u64 salt_{};
  u64 mixed_seed_{};
  FastNoise::SmartNode<> generator_{};
};

inline void read_i32(const YAML::Node& parent, const char* key, i32& value) {
  const YAML::Node child = parent[key];
  if (child) {
    value = child.as<i32>();
  }
}

inline void read_u32(const YAML::Node& parent, const char* key, u32& value) {
  const YAML::Node child = parent[key];
  if (child) {
    value = child.as<u32>();
  }
}

inline void read_f32(const YAML::Node& parent, const char* key, f32& value) {
  const YAML::Node child = parent[key];
  if (child) {
    value = child.as<f32>();
  }
}

} // namespace detail

[[nodiscard]] inline WorldGenerationConfigResult parse_world_generation_yaml(std::string_view text) {
  WorldGenerationConfig config;

  try {
    const YAML::Node root = YAML::Load(std::string{text});
    if (!root || root.IsNull()) {
      return WorldGenerationConfigResult::failure({"world generation YAML is empty"});
    }

    const YAML::Node id = root["id"];
    if (id) {
      config.id = id.as<std::string>();
    }
    detail::read_u32(root, "version", config.version);
    const YAML::Node seed_salt = root["seed_salt"];
    if (seed_salt) {
      config.seed_salt = seed_salt.as<std::string>();
    }

    const YAML::Node vertical = root["vertical"];
    if (vertical) {
      detail::read_i32(vertical, "min_y", config.min_y);
      detail::read_i32(vertical, "max_y", config.max_y);
      detail::read_i32(vertical, "sea_level", config.sea_level);
    }

    const YAML::Node density = root["density"];
    if (density) {
      detail::read_i32(density, "base_height", config.base_height);
      detail::read_i32(density, "height_scale", config.height_scale);
    }

    const YAML::Node terrain_noise = root["noise"] ? root["noise"]["terrain"] : YAML::Node{};
    if (terrain_noise) {
      const YAML::Node type = terrain_noise["type"];
      if (type) {
        config.terrain_noise_type = type.as<std::string>();
      }
      detail::read_f32(terrain_noise, "frequency", config.terrain_frequency);
      detail::read_u32(terrain_noise, "octaves", config.terrain_octaves);
      detail::read_f32(terrain_noise, "amplitude", config.terrain_amplitude);
      detail::read_f32(terrain_noise, "lacunarity", config.terrain_lacunarity);
      detail::read_f32(terrain_noise, "gain", config.terrain_gain);
    }

    const YAML::Node rules = root["surface"] ? root["surface"]["rules"] : YAML::Node{};
    if (rules) {
      if (!rules.IsSequence() || rules.size() == 0U) {
        return WorldGenerationConfigResult::failure({"surface.rules must be a non-empty sequence"});
      }

      const YAML::Node layers = rules[0]["layers"];
      if (!layers || !layers.IsSequence() || layers.size() == 0U) {
        return WorldGenerationConfigResult::failure({"surface rule must contain a non-empty layers sequence"});
      }

      for (const YAML::Node& layer_node : layers) {
        SurfaceLayerDefinition layer;
        const YAML::Node block = layer_node["block"];
        const YAML::Node depth = layer_node["depth"];
        if (!block || !depth) {
          return WorldGenerationConfigResult::failure({"surface layer must contain block and depth"});
        }
        layer.block_name = block.as<std::string>();
        if (depth.IsScalar() && depth.as<std::string>() == "remaining") {
          layer.remaining = true;
        } else {
          layer.depth = depth.as<i32>();
          if (layer.depth <= 0) {
            return WorldGenerationConfigResult::failure({"surface layer depth must be positive"});
          }
        }
        config.surface_layers.push_back(std::move(layer));
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
      config.surface_layers.push_back({"arvane:stone", 0, true});
    }

    return WorldGenerationConfigResult::success(std::move(config));
  } catch (const YAML::Exception& ex) {
    return WorldGenerationConfigResult::failure({"failed to parse world generation YAML: " + std::string{ex.what()}});
  }
}

[[nodiscard]] inline WorldGenerationPaletteResult resolve_world_generation_palette(
    const WorldGenerationConfig& config,
    const voxel::BlockRegistry& registry) {
  WorldGenerationPalette palette;
  palette.surface_layers.reserve(config.surface_layers.size());

  bool has_remaining = false;
  for (const SurfaceLayerDefinition& layer : config.surface_layers) {
    const std::optional<voxel::BlockVariantID> block = registry.find_variant(layer.block_name);
    if (!block) {
      return WorldGenerationPaletteResult::failure(
          {"world generation references unknown block variant: " + layer.block_name});
    }
    has_remaining = has_remaining || layer.remaining;
    palette.surface_layers.push_back({block.value(), layer.depth, layer.remaining});
  }

  if (!has_remaining) {
    return WorldGenerationPaletteResult::failure({"surface layers must include a remaining layer"});
  }

  return WorldGenerationPaletteResult::success(std::move(palette));
}

[[nodiscard]] inline i32 terrain_height_at(
    const WorldGenerationConfig& config,
    u64 seed,
    i32 world_x,
    i32 world_z) noexcept {
  const detail::TerrainNoiseSampler sampler{config, seed};
  const f32 terrain = sampler.terrain(world_x, world_z);
  const f32 height = static_cast<f32>(config.base_height) +
                     terrain * static_cast<f32>(config.height_scale);
  const i32 rounded = static_cast<i32>(std::floor(height + 0.5F));
  return rounded < config.min_y ? config.min_y : (rounded > config.max_y ? config.max_y : rounded);
}

[[nodiscard]] inline voxel::BlockVariantID choose_surface_block(
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

[[nodiscard]] inline GeneratedChunkResult generate_chunk(const ChunkGenerationRequest& request) {
  if (request.palette.surface_layers.empty()) {
    return GeneratedChunkResult::failure({"chunk generation request has no resolved surface layers"});
  }

  GeneratedChunk chunk;
  chunk.world_id = request.world_id;
  chunk.seed = request.seed;
  chunk.generator_version = request.config.version;
  chunk.coord = request.coord;
  chunk.blocks.fill(voxel::AirBlockVariantID);
  const detail::TerrainNoiseSampler terrain_sampler{request.config, request.seed};

  for (i32 z = 0; z < ChunkSize; ++z) {
    for (i32 x = 0; x < ChunkSize; ++x) {
      const i32 world_x = request.coord.value.x * ChunkSize + x;
      const i32 world_z = request.coord.value.z * ChunkSize + z;
      const f32 terrain = terrain_sampler.terrain(world_x, world_z);
      const f32 height = static_cast<f32>(request.config.base_height) +
                         terrain * static_cast<f32>(request.config.height_scale);
      const i32 rounded = static_cast<i32>(std::floor(height + 0.5F));
      const i32 surface_y = rounded < request.config.min_y
                                ? request.config.min_y
                                : (rounded > request.config.max_y ? request.config.max_y : rounded);

      for (i32 y = 0; y < ChunkSize; ++y) {
        const SectionLocalPos local{{x, y, z}};
        const i32 world_y = request.coord.value.y * ChunkSize + y;
        if (world_y < request.config.min_y || world_y > request.config.max_y || world_y > surface_y) {
          continue;
        }

        const i32 depth_from_surface = surface_y - world_y;
        chunk.blocks[voxel::local_index(local)] =
            choose_surface_block(request.palette, depth_from_surface);
      }
    }
  }

  return GeneratedChunkResult::success(std::move(chunk));
}

} // namespace arv::world
