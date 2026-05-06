#include "benchmark_support.hpp"

#include "arv/protocol/chunk_streaming.hpp"

#include <benchmark/benchmark.h>

namespace {

[[nodiscard]] arv::protocol::ChunkSectionData make_chunk_section_data(bool has_lighting) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const arv::voxel::BlockVariantID stone =
      arv::bench_support::find_variant(registry, "arvane:stone");
  const arv::voxel::BlockVariantID dirt =
      arv::bench_support::find_variant(registry, "arvane:dirt");

  arv::protocol::ChunkSectionData data;
  data.world_id = arv::WorldID{1};
  data.coord = {{3, 0, -2}};
  data.chunk_version = 42;
  data.chunk_content_version = 42;
  data.chunk_light_version = 9;
  data.has_lighting = has_lighting;
  data.blocks = arv::bench_support::noisy_terrain_blocks(stone, dirt);
  data.lights = arv::bench_support::filled_lights(
      arv::voxel::pack_light(3, 5, 7, 12));
  return data;
}

void BM_ChunkPacketEncodeNoLighting(benchmark::State& state) {
  const arv::protocol::ChunkSectionData data = make_chunk_section_data(false);
  for (auto _ : state) {
    std::vector<arv::u8> bytes = arv::protocol::serialize_chunk_section_data(data);
    benchmark::DoNotOptimize(bytes.data());
    benchmark::DoNotOptimize(bytes.size());
  }
}

void BM_ChunkPacketEncodeWithLighting(benchmark::State& state) {
  const arv::protocol::ChunkSectionData data = make_chunk_section_data(true);
  for (auto _ : state) {
    std::vector<arv::u8> bytes = arv::protocol::serialize_chunk_section_data(data);
    benchmark::DoNotOptimize(bytes.data());
    benchmark::DoNotOptimize(bytes.size());
  }
}

void BM_ChunkPacketDecodeWithLighting(benchmark::State& state) {
  const arv::protocol::ChunkSectionData data = make_chunk_section_data(true);
  const std::vector<arv::u8> bytes = arv::protocol::serialize_chunk_section_data(data);
  for (auto _ : state) {
    auto decoded = arv::protocol::deserialize_chunk_section_data(bytes);
    benchmark::DoNotOptimize(decoded);
    if (!decoded) {
      state.SkipWithError(decoded.error().message.c_str());
      break;
    }
    benchmark::DoNotOptimize(decoded.value().blocks.data());
  }
}

} // namespace

BENCHMARK(BM_ChunkPacketEncodeNoLighting);
BENCHMARK(BM_ChunkPacketEncodeWithLighting);
BENCHMARK(BM_ChunkPacketDecodeWithLighting);
