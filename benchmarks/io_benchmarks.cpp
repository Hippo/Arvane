#include "benchmark_support.hpp"

#include <benchmark/benchmark.h>

namespace {

void BM_RegionEncode(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const std::vector<arv::persistence::SavedChunkSection> chunks =
      arv::bench_support::make_saved_chunks(registry, 16U);

  for (auto _ : state) {
    auto encoded = arv::persistence::encode_region_file({0, 0}, chunks, registry, 0);
    benchmark::DoNotOptimize(encoded);
    if (!encoded) {
      state.SkipWithError(encoded.error().message.c_str());
      break;
    }
    benchmark::DoNotOptimize(encoded.value().data());
    benchmark::DoNotOptimize(encoded.value().size());
  }
}

void BM_RegionDecode(benchmark::State& state) {
  const arv::voxel::BlockRegistry registry = arv::bench_support::make_registry();
  const std::vector<arv::persistence::SavedChunkSection> chunks =
      arv::bench_support::make_saved_chunks(registry, 16U);
  auto encoded = arv::persistence::encode_region_file({0, 0}, chunks, registry, 0);
  if (!encoded) {
    state.SkipWithError(encoded.error().message.c_str());
    return;
  }

  for (auto _ : state) {
    auto decoded = arv::persistence::decode_region_file(encoded.value(), registry, 0);
    benchmark::DoNotOptimize(decoded);
    if (!decoded) {
      state.SkipWithError(decoded.error().message.c_str());
      break;
    }
    benchmark::DoNotOptimize(decoded.value().chunks.data());
    benchmark::DoNotOptimize(decoded.value().chunks.size());
  }
}

} // namespace

BENCHMARK(BM_RegionEncode);
BENCHMARK(BM_RegionDecode);
