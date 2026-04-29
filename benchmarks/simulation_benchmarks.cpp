#include "benchmark_support.hpp"

#include <benchmark/benchmark.h>

namespace {

void place_floor(
    arv::world::WorldRuntime& world,
    arv::voxel::BlockVariantID stone,
    arv::i32 min_x,
    arv::i32 max_x,
    arv::i32 z) {
  for (arv::i32 x = min_x; x <= max_x; ++x) {
    world.set_block({{x, 0, z}}, stone);
  }
}

void BM_FluidTickSparseChunks(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();

  for (auto _ : state) {
    state.PauseTiming();
    arv::world::WorldRuntime world{arv::WorldID{1}};
    for (arv::i32 x = 0; x < 4; ++x) {
      arv::bench_support::make_active_chunk(world, {{x, 0, 0}});
    }
    place_floor(world, stone, 0, 63, 1);
    world.place_fluid_source({{1, 1, 1}}, water);
    state.ResumeTiming();

    arv::u32 processed = world.process_fluid_updates(registry, water, 512);
    benchmark::DoNotOptimize(processed);
    benchmark::DoNotOptimize(world.pending_fluid_update_count());
  }
}

void BM_FluidTickDenseActiveAreas(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();

  for (auto _ : state) {
    state.PauseTiming();
    arv::world::WorldRuntime world{arv::WorldID{1}};
    for (arv::i32 z = 0; z < 2; ++z) {
      for (arv::i32 x = 0; x < 2; ++x) {
        arv::bench_support::make_active_chunk(world, {{x, 0, z}});
      }
    }
    for (arv::i32 z = 0; z < 32; ++z) {
      place_floor(world, stone, 0, 31, z);
    }
    for (arv::i32 z = 2; z < 30; z += 4) {
      for (arv::i32 x = 2; x < 30; x += 4) {
        world.place_fluid_source({{x, 1, z}}, water);
      }
    }
    state.ResumeTiming();

    arv::u32 processed = world.process_fluid_updates(registry, water, 4096);
    benchmark::DoNotOptimize(processed);
    benchmark::DoNotOptimize(world.pending_fluid_update_count());
  }
}

void BM_BoundedUpdateQueueBehavior(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::FluidDefinition water = arv::voxel::make_water_fluid_definition();

  for (auto _ : state) {
    state.PauseTiming();
    arv::world::WorldRuntime world{arv::WorldID{1}};
    world.ensure_chunk({{0, 0, 0}});
    for (arv::u32 index = 0; index < 4096U; ++index) {
      world.enqueue_fluid_update(
          {{
              static_cast<arv::i32>(index & 15U),
              static_cast<arv::i32>((index >> 4U) & 15U),
              static_cast<arv::i32>((index >> 8U) & 15U),
          }},
          arv::world::FluidUpdateReason::NeighborChanged);
    }
    state.ResumeTiming();

    arv::u32 processed = world.process_fluid_updates(registry, water, 512);
    benchmark::DoNotOptimize(processed);
    benchmark::DoNotOptimize(world.pending_fluid_update_count());
    benchmark::DoNotOptimize(world.total_deferred_boundary_fluid_event_count());
  }
}

} // namespace

BENCHMARK(BM_FluidTickSparseChunks);
BENCHMARK(BM_FluidTickDenseActiveAreas);
BENCHMARK(BM_BoundedUpdateQueueBehavior);
