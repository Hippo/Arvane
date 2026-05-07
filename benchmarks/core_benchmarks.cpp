#include "benchmark_support.hpp"

#include "arv/render/render_types.hpp"

#include <benchmark/benchmark.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace {

[[nodiscard]] std::vector<arv::ChunkSectionCoord> make_lookup_keys() {
  std::vector<arv::ChunkSectionCoord> keys;
  keys.reserve(4096U);
  for (arv::i32 z = 0; z < 16; ++z) {
    for (arv::i32 y = 0; y < 16; ++y) {
      for (arv::i32 x = 0; x < 16; ++x) {
        keys.push_back({{x - 8, y - 8, z - 8}});
      }
    }
  }
  return keys;
}

void BM_HashMapRegistryLookup(benchmark::State& state) {
  const std::vector<arv::ChunkSectionCoord> keys = make_lookup_keys();
  arv::HashMap<arv::ChunkSectionCoord, arv::u32> map;
  map.reserve(keys.size());
  for (arv::usize index = 0; index < keys.size(); ++index) {
    map.emplace(keys[index], static_cast<arv::u32>(index));
  }

  arv::u64 sum = 0;
  arv::usize index = 0;
  for (auto _ : state) {
    const auto found = map.find(keys[index & (keys.size() - 1U)]);
    sum += found->second;
    ++index;
  }
  benchmark::DoNotOptimize(sum);
}

void BM_UnorderedMapRegistryLookup(benchmark::State& state) {
  const std::vector<arv::ChunkSectionCoord> keys = make_lookup_keys();
  std::unordered_map<arv::ChunkSectionCoord, arv::u32> map;
  map.reserve(keys.size());
  for (arv::usize index = 0; index < keys.size(); ++index) {
    map.emplace(keys[index], static_cast<arv::u32>(index));
  }

  arv::u64 sum = 0;
  arv::usize index = 0;
  for (auto _ : state) {
    const auto found = map.find(keys[index & (keys.size() - 1U)]);
    sum += found->second;
    ++index;
  }
  benchmark::DoNotOptimize(sum);
}

void BM_ChunkSectionCoordHash(benchmark::State& state) {
  const std::vector<arv::ChunkSectionCoord> keys = make_lookup_keys();
  std::hash<arv::ChunkSectionCoord> hasher;
  arv::usize sum = 0;
  arv::usize index = 0;
  for (auto _ : state) {
    sum ^= hasher(keys[index & (keys.size() - 1U)]);
    ++index;
  }
  benchmark::DoNotOptimize(sum);
}

void BM_NearbyChunkLookup(benchmark::State& state) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height({-2, 2});
  for (arv::i32 z = -8; z <= 8; ++z) {
    for (arv::i32 y = -2; y <= 2; ++y) {
      for (arv::i32 x = -8; x <= 8; ++x) {
        world.ensure_chunk({{x, y, z}});
      }
    }
  }

  const std::vector<arv::ChunkSectionCoord> centers = make_lookup_keys();
  arv::usize index = 0;
  arv::usize hits = 0;
  for (auto _ : state) {
    const arv::ChunkSectionCoord center = centers[index & (centers.size() - 1U)];
    for (arv::i32 z = -1; z <= 1; ++z) {
      for (arv::i32 y = -1; y <= 1; ++y) {
        for (arv::i32 x = -1; x <= 1; ++x) {
          if (x == 0 && y == 0 && z == 0) {
            continue;
          }
          const arv::ChunkSectionCoord coord{{
              center.value.x + x,
              center.value.y + y,
              center.value.z + z,
          }};
          hits += world.find_chunk(coord) != nullptr ? 1U : 0U;
        }
      }
    }
    ++index;
  }
  benchmark::DoNotOptimize(hits);
}

void BM_BlockRegistryStableNameLookup(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  std::vector<std::string> names;
  names.reserve(32U);
  const std::vector<std::string> colors{
      "white", "orange", "magenta", "light_blue", "yellow", "lime",
      "pink", "gray", "light_gray", "cyan", "purple", "blue",
      "brown", "green", "red", "black"};
  for (const std::string& color : colors) {
    names.push_back("arvane:palette_block[color=" + color + ",lit=false]");
    names.push_back("arvane:palette_block[color=" + color + ",lit=true]");
  }

  arv::u64 sum = 0;
  arv::usize index = 0;
  for (auto _ : state) {
    auto variant = registry.find_variant(names[index & (names.size() - 1U)]);
    benchmark::DoNotOptimize(variant);
    sum += variant ? variant->value : 0U;
    ++index;
  }
  benchmark::DoNotOptimize(sum);
}

void BM_PackedVertexGeneration(benchmark::State& state) {
  arv::u32 sum = 0;
  arv::u32 index = 0;
  for (auto _ : state) {
    const arv::SectionLocalPos block{{
        static_cast<arv::i32>(index & 15U),
        static_cast<arv::i32>((index >> 4U) & 15U),
        static_cast<arv::i32>((index >> 8U) & 15U),
    }};
    const arv::Vec3i model_position{
        static_cast<arv::i32>((index >> 1U) & 16U),
        static_cast<arv::i32>((index >> 2U) & 16U),
        static_cast<arv::i32>((index >> 3U) & 16U),
    };
    const arv::render::ChunkVertex vertex = arv::render::make_chunk_vertex(
        block,
        model_position,
        static_cast<arv::u16>(index & 0xFFFFU),
        static_cast<arv::u16>((index * 3U) & 0xFFFFU),
        static_cast<arv::u16>((index >> 16U) & 0xFFFFU),
        arv::voxel::BlockFace::PositiveY,
        static_cast<arv::u8>(index & 3U),
        static_cast<arv::u8>(index & 15U),
        static_cast<arv::u8>((index >> 4U) & 15U),
        static_cast<arv::u8>((index >> 8U) & 15U),
        static_cast<arv::u8>((index >> 12U) & 15U));
    sum ^= vertex.pos_ao ^ vertex.uv ^ vertex.light_face_material_hints;
    ++index;
  }
  benchmark::DoNotOptimize(sum);
}

void BM_BlockVariantPaletteLookup(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  std::vector<arv::voxel::BlockVariantID> variants;
  variants.reserve(registry.variant_count());
  for (arv::u32 id = 0; id < registry.variant_count(); ++id) {
    variants.push_back(arv::voxel::BlockVariantID{id});
  }

  arv::u64 sum = 0;
  arv::usize index = 0;
  for (auto _ : state) {
    const auto* variant = registry.variant(variants[index % variants.size()]);
    benchmark::DoNotOptimize(variant);
    sum += variant != nullptr && variant->properties.full_cube ? 1U : 0U;
    ++index;
  }
  benchmark::DoNotOptimize(sum);
}

} // namespace

BENCHMARK(BM_HashMapRegistryLookup);
BENCHMARK(BM_UnorderedMapRegistryLookup);
BENCHMARK(BM_ChunkSectionCoordHash);
BENCHMARK(BM_NearbyChunkLookup);
BENCHMARK(BM_BlockRegistryStableNameLookup);
BENCHMARK(BM_PackedVertexGeneration);
BENCHMARK(BM_BlockVariantPaletteLookup);
