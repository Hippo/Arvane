#include "benchmark_support.hpp"

#include "arv/world/world_generation.hpp"

#include <benchmark/benchmark.h>

#include <stdexcept>
#include <utility>

namespace {

arv::world::ChunkGenerationRequest make_worldgen_request(
    const arv::voxel::BlockRegistry& registry,
    arv::ChunkSectionCoord coord = {{0, 0, 0}}) {
  auto parsed = arv::world::parse_world_generation_yaml(R"yaml(
id: arvane:benchmark_overworld
version: 1
seed_salt: "benchmark"
vertical:
  min_y: -64
  max_y: 128
  sea_level: 32
noise:
  terrain:
    frequency: 0.035
    octaves: 3
density:
  base_height: 20
  height_scale: 24
surface:
  rules:
    - layers:
        - block: arvane:grass
          depth: 1
        - block: arvane:dirt
          depth: 3
        - block: arvane:stone
          depth: remaining
)yaml");
  if (!parsed) {
    throw std::runtime_error(parsed.error().message);
  }

  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = 0xBADC0FFEEULL;
  request.coord = arv::chunk_to_column(coord);
  request.config = std::move(parsed.value());
  request.min_chunk_y = arv::floor_div(request.config.min_y, arv::ChunkSize);
  request.max_chunk_y = arv::floor_div(request.config.max_y, arv::ChunkSize);

  auto palette = arv::world::resolve_world_generation_palette(request.config, registry);
  if (!palette) {
    throw std::runtime_error(palette.error().message);
  }
  request.palette = std::move(palette.value());
  return request;
}

void BM_ChunkAllocationReset(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  for (auto _ : state) {
    arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
    chunk.replace_generated_blocks(arv::bench_support::filled_blocks(stone));
    chunk.replace_generated_blocks(arv::bench_support::filled_blocks(arv::voxel::AirBlockVariantID));
    benchmark::DoNotOptimize(chunk.storage_identity());
  }
}

void BM_BlockGetSet(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  const arv::voxel::BlockVariantID dirt =
      arv::bench_support::find_variant(registry, "arvane:dirt");
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};

  arv::usize index = 0;
  arv::u64 sum = 0;
  for (auto _ : state) {
    const arv::SectionLocalPos local =
        arv::bench_support::local_from_index(index & (arv::voxel::ChunkVolume - 1U));
    const arv::voxel::BlockVariantID next = (index & 1U) == 0U ? stone : dirt;
    benchmark::DoNotOptimize(chunk.set_block(local, next));
    sum += chunk.block(local).value;
    ++index;
  }
  benchmark::DoNotOptimize(sum);
}

void BM_SectionSnapshotCreation(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  chunk.replace_generated_blocks(arv::bench_support::filled_blocks(stone));

  for (auto _ : state) {
    const arv::voxel::ChunkSnapshot snapshot =
        arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
    benchmark::DoNotOptimize(snapshot.center_storage_identity());
  }
}

void BM_CopyOnWriteSnapshotPath(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  const arv::voxel::BlockVariantID dirt =
      arv::bench_support::find_variant(registry, "arvane:dirt");
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  chunk.replace_generated_blocks(arv::bench_support::filled_blocks(stone));

  arv::usize index = 0;
  for (auto _ : state) {
    const arv::voxel::ChunkSnapshot snapshot =
        arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
    const arv::SectionLocalPos local =
        arv::bench_support::local_from_index(index & (arv::voxel::ChunkVolume - 1U));
    const arv::voxel::BlockVariantID next = (index & 1U) == 0U ? dirt : stone;
    benchmark::DoNotOptimize(snapshot.center_storage_identity());
    benchmark::DoNotOptimize(chunk.set_block(local, next));
    ++index;
  }
}

void BM_NeighborBoundaryExtraction(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::world::WorldRuntime world = arv::bench_support::make_neighborhood_world(registry);

  for (auto _ : state) {
    auto snapshot = world.make_meshing_snapshot({{0, 0, 0}});
    benchmark::DoNotOptimize(snapshot->center_storage_identity());
  }
}

void BM_GenerateChunkColumn(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  arv::world::ChunkGenerationRequest request = make_worldgen_request(registry);
  arv::i32 chunk_x = 0;
  for (auto _ : state) {
    request.coord = {chunk_x, chunk_x / 3};
    auto generated = arv::world::generate_chunk(request);
    if (!generated) {
      state.SkipWithError(generated.error().message.c_str());
      break;
    }
    benchmark::DoNotOptimize(generated.value().surface_y_by_column.data());
    benchmark::DoNotOptimize(generated.value().sections.size());
    ++chunk_x;
  }
}

} // namespace

BENCHMARK(BM_ChunkAllocationReset);
BENCHMARK(BM_BlockGetSet);
BENCHMARK(BM_SectionSnapshotCreation);
BENCHMARK(BM_CopyOnWriteSnapshotPath);
BENCHMARK(BM_NeighborBoundaryExtraction);
BENCHMARK(BM_GenerateChunkColumn);
