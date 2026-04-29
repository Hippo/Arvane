#include "benchmark_support.hpp"

#include <benchmark/benchmark.h>

namespace {

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

} // namespace

BENCHMARK(BM_ChunkAllocationReset);
BENCHMARK(BM_BlockGetSet);
BENCHMARK(BM_SectionSnapshotCreation);
BENCHMARK(BM_CopyOnWriteSnapshotPath);
BENCHMARK(BM_NeighborBoundaryExtraction);
