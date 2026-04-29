#include "benchmark_support.hpp"

#include "arv/render/chunk_mesher.hpp"

#include <benchmark/benchmark.h>

namespace {

void consume_mesh(const arv::render::ChunkMeshData& mesh) {
  benchmark::DoNotOptimize(mesh.opaque.vertex_bytes.data());
  benchmark::DoNotOptimize(mesh.opaque.indices.data());
  benchmark::DoNotOptimize(mesh.transparent.vertex_bytes.data());
  benchmark::DoNotOptimize(mesh.transparent.indices.data());
  benchmark::DoNotOptimize(mesh.opaque.vertex_bytes.size());
  benchmark::DoNotOptimize(mesh.transparent.vertex_bytes.size());
}

void BM_EmptySectionMesh(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  const arv::voxel::ChunkSnapshot snapshot =
      arv::voxel::make_chunk_snapshot_without_neighbors(chunk);

  for (auto _ : state) {
    const arv::render::ChunkMeshData mesh =
        arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
    consume_mesh(mesh);
  }
}

void BM_SolidCubeSectionMesh(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  chunk.replace_generated_blocks(arv::bench_support::filled_blocks(stone));
  const arv::voxel::ChunkSnapshot snapshot =
      arv::voxel::make_chunk_snapshot_without_neighbors(chunk);

  for (auto _ : state) {
    const arv::render::ChunkMeshData mesh =
        arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
    consume_mesh(mesh);
  }
}

void BM_NoisyTerrainSectionMesh(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  const arv::voxel::BlockVariantID dirt =
      arv::bench_support::find_variant(registry, "arvane:dirt");
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  arv::bench_support::fill_noisy_terrain(chunk, stone, dirt);
  const arv::voxel::ChunkSnapshot snapshot =
      arv::voxel::make_chunk_snapshot_without_neighbors(chunk);

  for (auto _ : state) {
    const arv::render::ChunkMeshData mesh =
        arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
    consume_mesh(mesh);
  }
}

void BM_AOEnabledMesh(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  for (arv::i32 z = 2; z < 14; ++z) {
    for (arv::i32 y = 2; y < 14; ++y) {
      for (arv::i32 x = 2; x < 14; ++x) {
        if ((arv::bench_support::mixed_index(static_cast<arv::u32>(x + y * 31 + z * 131)) & 3U) != 0U) {
          chunk.set_block({{x, y, z}}, stone);
        }
      }
    }
  }
  const arv::voxel::ChunkSnapshot snapshot =
      arv::voxel::make_chunk_snapshot_without_neighbors(chunk);

  for (auto _ : state) {
    const arv::render::ChunkMeshData mesh =
        arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
    consume_mesh(mesh);
  }
}

void BM_TransparentMeshPath(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID glass =
      arv::bench_support::find_variant(registry, "arvane:glass");
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  for (arv::usize index = 0; index < arv::voxel::ChunkVolume; ++index) {
    if ((arv::bench_support::mixed_index(static_cast<arv::u32>(index)) & 1U) == 0U) {
      chunk.set_block(arv::bench_support::local_from_index(index), glass);
    }
  }
  const arv::voxel::ChunkSnapshot snapshot =
      arv::voxel::make_chunk_snapshot_without_neighbors(chunk);

  for (auto _ : state) {
    const arv::render::ChunkMeshData mesh =
        arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
    consume_mesh(mesh);
  }
}

void BM_RemeshAfterSingleBlockEdit(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  const arv::voxel::BlockVariantID dirt =
      arv::bench_support::find_variant(registry, "arvane:dirt");
  arv::voxel::ChunkSection chunk{{{0, 0, 0}}};
  arv::bench_support::fill_noisy_terrain(chunk, stone, dirt);

  arv::usize index = 0;
  for (auto _ : state) {
    chunk.set_block({{8, 8, 8}}, (index & 1U) == 0U ? stone : dirt);
    const arv::voxel::ChunkSnapshot snapshot =
        arv::voxel::make_chunk_snapshot_without_neighbors(chunk);
    const arv::render::ChunkMeshData mesh =
        arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot, registry);
    consume_mesh(mesh);
    ++index;
  }
}

} // namespace

BENCHMARK(BM_EmptySectionMesh);
BENCHMARK(BM_SolidCubeSectionMesh);
BENCHMARK(BM_NoisyTerrainSectionMesh);
BENCHMARK(BM_AOEnabledMesh);
BENCHMARK(BM_TransparentMeshPath);
BENCHMARK(BM_RemeshAfterSingleBlockEdit);
