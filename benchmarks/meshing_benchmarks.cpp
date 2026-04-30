#include "benchmark_support.hpp"

#include "arv/render/chunk_mesher.hpp"

#include <benchmark/benchmark.h>

#include <array>

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

void BM_StableCameraPathMeshing(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  const arv::voxel::BlockVariantID dirt =
      arv::bench_support::find_variant(registry, "arvane:dirt");
  arv::world::WorldRuntime world{arv::WorldID{1}};
  for (arv::i32 z = -4; z <= 4; ++z) {
    for (arv::i32 x = -4; x <= 4; ++x) {
      arv::voxel::ChunkSection& chunk = world.ensure_chunk({{x, 0, z}});
      arv::bench_support::fill_noisy_terrain(chunk, stone, dirt);
    }
  }

  constexpr std::array<arv::ChunkCoord, 8> CameraPath{{
      {{-2, 0, -2}},
      {{-1, 0, -2}},
      {{0, 0, -1}},
      {{1, 0, 0}},
      {{2, 0, 1}},
      {{1, 0, 2}},
      {{0, 0, 1}},
      {{-1, 0, 0}},
  }};

  arv::usize path_index = 0;
  arv::u64 total_meshes = 0;
  arv::u64 total_indices = 0;
  for (auto _ : state) {
    const arv::ChunkCoord center = CameraPath[path_index++ % CameraPath.size()];
    for (arv::i32 z = center.value.z - 1; z <= center.value.z + 1; ++z) {
      for (arv::i32 x = center.value.x - 1; x <= center.value.x + 1; ++x) {
        auto snapshot = world.make_meshing_snapshot(
            {{x, 0, z}},
            arv::world::MissingNeighborPolicy::TreatMissingAsAir);
        if (!snapshot.has_value()) {
          continue;
        }
        const arv::render::ChunkMeshData mesh =
            arv::render::build_chunk_mesh(arv::WorldID{1}, snapshot.value(), registry);
        total_indices += mesh.opaque.indices.size() + mesh.transparent.indices.size();
        ++total_meshes;
        consume_mesh(mesh);
      }
    }
  }
  state.counters["meshes"] = benchmark::Counter(
      static_cast<double>(total_meshes),
      benchmark::Counter::kIsRate);
  state.counters["indices"] = benchmark::Counter(
      static_cast<double>(total_indices),
      benchmark::Counter::kIsRate);
}

} // namespace

BENCHMARK(BM_EmptySectionMesh);
BENCHMARK(BM_SolidCubeSectionMesh);
BENCHMARK(BM_NoisyTerrainSectionMesh);
BENCHMARK(BM_AOEnabledMesh);
BENCHMARK(BM_TransparentMeshPath);
BENCHMARK(BM_RemeshAfterSingleBlockEdit);
BENCHMARK(BM_StableCameraPathMeshing);
