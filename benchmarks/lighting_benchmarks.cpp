#include "benchmark_support.hpp"

#include <benchmark/benchmark.h>

namespace {

void consume_lighting_result(const arv::world::LightingResult& result) {
  benchmark::DoNotOptimize(result.chunks.data());
  benchmark::DoNotOptimize(result.chunks.size());
}

void BM_InitialSunlightFill(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::voxel::ChunkSection& chunk = world.ensure_chunk({{0, 0, 0}});
  chunk.mark_dirty(arv::voxel::ChunkDirtyFlags::Lighting);
  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();

  for (auto _ : state) {
    const arv::world::LightingResult result =
        arv::world::WorldRuntime::build_lighting_result(snapshot, registry);
    consume_lighting_result(result);
  }
}

void BM_LocalBlockLightPropagation(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID glowstone =
      arv::bench_support::find_variant(registry, "arvane:glowstone");
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  world.set_block({{8, 8, 8}}, glowstone);
  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();

  for (auto _ : state) {
    const arv::world::LightingResult result =
        arv::world::WorldRuntime::build_lighting_result(snapshot, registry);
    consume_lighting_result(result);
  }
}

void BM_CrossSectionBoundaryLightUpdate(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID glowstone =
      arv::bench_support::find_variant(registry, "arvane:glowstone");
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  world.ensure_chunk({{1, 0, 0}});
  world.set_block({{15, 8, 8}}, glowstone);
  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();

  for (auto _ : state) {
    const arv::world::LightingResult result =
        arv::world::WorldRuntime::build_lighting_result(snapshot, registry);
    consume_lighting_result(result);
  }
}

void BM_RGBBlockLightUpdate(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID glowstone =
      arv::bench_support::find_variant(registry, "arvane:glowstone");
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  for (arv::i32 z = 3; z < 14; z += 5) {
    for (arv::i32 y = 3; y < 14; y += 5) {
      for (arv::i32 x = 3; x < 14; x += 5) {
        world.set_block({{x, y, z}}, glowstone);
      }
    }
  }
  const arv::world::LightingSnapshot snapshot = world.make_lighting_snapshot();

  for (auto _ : state) {
    const arv::world::LightingResult result =
        arv::world::WorldRuntime::build_lighting_result(snapshot, registry);
    consume_lighting_result(result);
  }
}

} // namespace

BENCHMARK(BM_InitialSunlightFill);
BENCHMARK(BM_LocalBlockLightPropagation);
BENCHMARK(BM_CrossSectionBoundaryLightUpdate);
BENCHMARK(BM_RGBBlockLightUpdate);
