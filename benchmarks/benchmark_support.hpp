#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/types.hpp"
#include "arv/persistence/region_file.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/light.hpp"
#include "arv/voxel/storage.hpp"
#include "arv/world/world_runtime.hpp"

#include <array>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace arv::bench_support {

[[nodiscard]] inline u32 mixed_index(u32 value) noexcept {
  value ^= value >> 16U;
  value *= 0x7feb352dU;
  value ^= value >> 15U;
  value *= 0x846ca68bU;
  value ^= value >> 16U;
  return value;
}

[[nodiscard]] inline SectionLocalPos local_from_index(usize index) noexcept {
  const i32 x = static_cast<i32>(index % voxel::ChunkSide);
  index /= voxel::ChunkSide;
  const i32 y = static_cast<i32>(index % voxel::ChunkSide);
  const i32 z = static_cast<i32>(index / voxel::ChunkSide);
  return {{x, y, z}};
}

[[nodiscard]] inline voxel::BlockRegistry make_registry() {
  std::vector<voxel::BlockDefinition> definitions;
  definitions.push_back(voxel::make_air_definition());
  definitions.push_back(voxel::make_full_cube_definition("arvane:stone"));
  definitions.push_back(voxel::make_full_cube_definition("arvane:dirt"));
  definitions.push_back(voxel::make_full_cube_definition("arvane:grass"));
  definitions.push_back(voxel::make_full_cube_definition(
      "arvane:glass",
      voxel::RenderCategory::Transparent));

  voxel::BlockDefinition glowstone =
      voxel::make_full_cube_definition("arvane:glowstone");
  glowstone.properties.emits_light = true;
  glowstone.properties.emission_r = 15;
  glowstone.properties.emission_g = 10;
  glowstone.properties.emission_b = 5;
  definitions.push_back(std::move(glowstone));

  voxel::BlockDefinition palette =
      voxel::make_full_cube_definition("arvane:palette_block");
  palette.states.push_back({
      "color",
      {
          "white",
          "orange",
          "magenta",
          "light_blue",
          "yellow",
          "lime",
          "pink",
          "gray",
          "light_gray",
          "cyan",
          "purple",
          "blue",
          "brown",
          "green",
          "red",
          "black",
      },
  });
  palette.states.push_back({"lit", {"false", "true"}});
  definitions.push_back(std::move(palette));

  auto result = voxel::BlockRegistry::build(std::move(definitions));
  if (!result) {
    throw std::runtime_error(result.error().message);
  }
  return std::move(result.value());
}

[[nodiscard]] inline voxel::BlockVariantID find_variant(
    const voxel::BlockRegistry& registry,
    std::string_view name) {
  const auto found = registry.find_variant(name);
  if (!found) {
    throw std::runtime_error("benchmark block variant is missing");
  }
  return found.value();
}

[[nodiscard]] inline std::array<voxel::BlockVariantID, voxel::ChunkVolume> filled_blocks(
    voxel::BlockVariantID variant) {
  std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks;
  blocks.fill(variant);
  return blocks;
}

[[nodiscard]] inline std::array<voxel::LightWord, voxel::ChunkVolume> filled_lights(
    voxel::LightWord light) {
  std::array<voxel::LightWord, voxel::ChunkVolume> lights;
  lights.fill(light);
  return lights;
}

[[nodiscard]] inline std::array<voxel::BlockVariantID, voxel::ChunkVolume> noisy_terrain_blocks(
    voxel::BlockVariantID stone,
    voxel::BlockVariantID dirt) {
  std::array<voxel::BlockVariantID, voxel::ChunkVolume> blocks;
  blocks.fill(voxel::AirBlockVariantID);
  for (i32 z = 0; z < ChunkSize; ++z) {
    for (i32 x = 0; x < ChunkSize; ++x) {
      const i32 height = 4 + static_cast<i32>(mixed_index(static_cast<u32>(x + z * ChunkSize)) % 9U);
      for (i32 y = 0; y <= height && y < ChunkSize; ++y) {
        const SectionLocalPos local{{x, y, z}};
        blocks[voxel::local_index(local)] = y == height ? dirt : stone;
      }
    }
  }
  return blocks;
}

inline void fill_noisy_terrain(
    voxel::ChunkSection& chunk,
    voxel::BlockVariantID stone,
    voxel::BlockVariantID dirt) {
  chunk.replace_generated_blocks(noisy_terrain_blocks(stone, dirt));
}

[[nodiscard]] inline world::WorldRuntime make_neighborhood_world(
    const voxel::BlockRegistry& registry,
    voxel::BlockVariantID center_fill = voxel::AirBlockVariantID) {
  (void)registry;
  world::WorldRuntime world{WorldID{1}};
  for (i32 z = -1; z <= 1; ++z) {
    for (i32 y = -1; y <= 1; ++y) {
      for (i32 x = -1; x <= 1; ++x) {
        voxel::ChunkSection& chunk = world.ensure_chunk({{x, y, z}});
        if (x == 0 && y == 0 && z == 0 && center_fill != voxel::AirBlockVariantID) {
          chunk.replace_generated_blocks(filled_blocks(center_fill));
        }
      }
    }
  }
  return world;
}

inline void make_active_chunk(world::WorldRuntime& world, ChunkCoord coord) {
  world.ensure_chunk(coord);
  if (!world.set_chunk_lifecycle_state(coord, voxel::ChunkLifecycleState::Active)) {
    std::abort();
  }
}

[[nodiscard]] inline std::vector<persistence::SavedChunkSection> make_saved_chunks(
    const voxel::BlockRegistry& registry,
    usize count) {
  const voxel::BlockVariantID stone = find_variant(registry, "arvane:stone");
  const voxel::BlockVariantID dirt = find_variant(registry, "arvane:dirt");
  const voxel::BlockVariantID glass = find_variant(registry, "arvane:glass");

  std::vector<persistence::SavedChunkSection> chunks;
  chunks.reserve(count);
  for (usize chunk_index = 0; chunk_index < count; ++chunk_index) {
    persistence::SavedChunkSection chunk;
    chunk.coord = {{
        static_cast<i32>(chunk_index % 8U),
        static_cast<i32>((chunk_index / 8U) % 4U),
        static_cast<i32>(chunk_index / 32U),
    }};
    chunk.lighting_valid = (chunk_index % 2U) == 0U;
    for (usize index = 0; index < voxel::ChunkVolume; ++index) {
      const u32 selector = mixed_index(static_cast<u32>(index + chunk_index * 8191U)) % 11U;
      chunk.blocks[index] = selector == 0U ? glass : (selector < 6U ? stone : dirt);
      chunk.lights[index] = voxel::pack_light(
          static_cast<u8>(selector & 0x0FU),
          static_cast<u8>((selector + 3U) & 0x0FU),
          static_cast<u8>((selector + 7U) & 0x0FU),
          static_cast<u8>((index + chunk_index) & 0x0FU));
    }
    chunks.push_back(std::move(chunk));
  }
  return chunks;
}

} // namespace arv::bench_support
