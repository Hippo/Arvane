#include "benchmark_support.hpp"

#include "arv/sched/job_system.hpp"
#include "arv/asset/pack.hpp"
#include "arv/world/worldgen/chunk_generator.hpp"
#include "arv/world/worldgen/loader.hpp"
#include "arv/world/worldgen/terrain_query.hpp"
#include "arv/world/world_generation.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using BenchmarkClock = std::chrono::steady_clock;

[[nodiscard]] arv::u64 elapsed_ns(
    BenchmarkClock::time_point start,
    BenchmarkClock::time_point end = BenchmarkClock::now()) noexcept {
  return static_cast<arv::u64>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

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

struct ResolvedWorldgenFixture {
  arv::voxel::BlockRegistry registry{};
  std::shared_ptr<const arv::world::worldgen::ResolvedDimension> dimension{};
  arv::world::WorldGenerationConfig config{};
  std::vector<arv::ChunkCoord> region16{};
  std::vector<arv::ChunkCoord> region32{};
  std::vector<arv::ChunkCoord> spiral{};
  arv::ChunkCoord plains{};
  arv::ChunkCoord rolling{};
  arv::ChunkCoord mountains{};
  arv::ChunkCoord ocean{};
  arv::ChunkCoord forest{};
  arv::ChunkCoord biome_border{};
};

struct WorldgenVariantFixture {
  std::shared_ptr<const arv::world::worldgen::ResolvedDimension> dimension{};
  arv::world::WorldGenerationConfig config{};
  std::vector<arv::ChunkCoord> coords{};
};

struct WorldgenJobBenchmarkResult {
  arv::world::GeneratedChunkResult generated;
  arv::world::worldgen::WorldGenerationStats stats{};
  arv::u64 queue_wait_ns{};
  arv::u64 run_ns{};
};

[[nodiscard]] arv::voxel::BlockRegistry make_pack_block_registry(
    const arv::asset::PackManager& packs) {
  auto block_definition_text = packs.load_merged_json_array_text("blocks", "blocks");
  if (!block_definition_text) {
    throw std::runtime_error(block_definition_text.error().describe());
  }
  auto block_definitions =
      arv::voxel::parse_block_definitions_json(block_definition_text.value());
  if (!block_definitions) {
    throw std::runtime_error(block_definitions.error().message);
  }
  auto registry = arv::voxel::BlockRegistry::build(std::move(block_definitions.value()));
  if (!registry) {
    throw std::runtime_error(registry.error().message);
  }
  return std::move(registry.value());
}

[[nodiscard]] std::vector<arv::ChunkCoord> make_region_coords(arv::i32 side) {
  std::vector<arv::ChunkCoord> coords;
  coords.reserve(static_cast<arv::usize>(side * side));
  const arv::i32 half = side / 2;
  for (arv::i32 z = -half; z < side - half; ++z) {
    for (arv::i32 x = -half; x < side - half; ++x) {
      coords.push_back({x, z});
    }
  }
  return coords;
}

[[nodiscard]] std::vector<arv::ChunkCoord> make_spiral_coords(arv::i32 radius) {
  std::vector<arv::ChunkCoord> coords;
  coords.push_back({0, 0});
  for (arv::i32 ring = 1; ring <= radius; ++ring) {
    for (arv::i32 x = -ring; x <= ring; ++x) {
      coords.push_back({x, -ring});
    }
    for (arv::i32 z = -ring + 1; z <= ring; ++z) {
      coords.push_back({ring, z});
    }
    for (arv::i32 x = ring - 1; x >= -ring; --x) {
      coords.push_back({x, ring});
    }
    for (arv::i32 z = ring - 1; z > -ring; --z) {
      coords.push_back({-ring, z});
    }
  }
  return coords;
}

[[nodiscard]] bool profile_has_biome_tag(
    const arv::world::worldgen::ResolvedDimension& dimension,
    const arv::world::worldgen::TerrainProfile& profile,
    std::string_view tag) {
  for (const arv::usize biome_index : profile.biome_by_column) {
    if (biome_index < dimension.biomes.size() &&
        arv::world::worldgen::biome_has_tag(dimension.biomes[biome_index].raw, tag)) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] arv::ChunkCoord find_chunk_by_score(
    const arv::world::worldgen::ResolvedDimension& dimension,
    std::string_view tag,
    bool prefer_high,
    bool prefer_border) {
  arv::ChunkCoord best{0, 0};
  arv::i32 best_score = std::numeric_limits<arv::i32>::min();
  for (arv::i32 z = -48; z <= 48; ++z) {
    for (arv::i32 x = -48; x <= 48; ++x) {
      const arv::ChunkCoord coord{x, z};
      const auto profile =
          arv::world::worldgen::generate_terrain_profile(dimension, 0x5EED1234ULL, coord);
      if (!tag.empty() && !profile_has_biome_tag(dimension, profile, tag)) {
        continue;
      }
      arv::i32 score = prefer_high ? profile.max_surface_y : -profile.min_surface_y;
      if (prefer_border) {
        std::array<arv::usize, 8> seen{};
        arv::usize seen_count = 0;
        for (const arv::usize biome_index : profile.biome_by_column) {
          if (std::find(seen.begin(), seen.begin() + static_cast<std::ptrdiff_t>(seen_count), biome_index) ==
              seen.begin() + static_cast<std::ptrdiff_t>(seen_count)) {
            seen[seen_count++] = biome_index;
          }
        }
        score = static_cast<arv::i32>(seen_count) * 1000 +
                (profile.max_surface_y - profile.min_surface_y);
      }
      if (score > best_score) {
        best_score = score;
        best = coord;
      }
    }
  }
  return best;
}

[[nodiscard]] ResolvedWorldgenFixture make_resolved_worldgen_fixture() {
  auto packs = arv::asset::PackManager::load_from_packs_root("arvane_home/packs");
  if (!packs) {
    throw std::runtime_error(packs.error().describe());
  }
  ResolvedWorldgenFixture fixture;
  fixture.registry = make_pack_block_registry(packs.value());
  auto worldgen_registry =
      arv::world::worldgen::load_worldgen_registry_from_packs(packs.value());
  if (!worldgen_registry) {
    throw std::runtime_error(worldgen_registry.error().describe());
  }
  auto resolved = arv::world::worldgen::resolve_dimension(
      worldgen_registry.value(),
      "arvane:worldgen/dimension/overworld",
      fixture.registry);
  if (!resolved) {
    throw std::runtime_error(resolved.error().describe());
  }
  fixture.dimension = std::move(resolved.value());
  fixture.config = arv::world::config_from_resolved_dimension(fixture.dimension);
  fixture.region16 = make_region_coords(16);
  fixture.region32 = make_region_coords(32);
  fixture.spiral = make_spiral_coords(10);
  fixture.plains = find_chunk_by_score(*fixture.dimension, "arvane:biome/grassy", false, false);
  fixture.rolling = find_chunk_by_score(*fixture.dimension, "arvane:biome/land", true, false);
  fixture.mountains = find_chunk_by_score(*fixture.dimension, "arvane:biome/rocky", true, false);
  fixture.ocean = find_chunk_by_score(*fixture.dimension, "arvane:biome/aquatic", false, false);
  fixture.forest = find_chunk_by_score(*fixture.dimension, "arvane:biome/forested", false, false);
  fixture.biome_border = find_chunk_by_score(*fixture.dimension, "", false, true);
  return fixture;
}

[[nodiscard]] const ResolvedWorldgenFixture& resolved_worldgen_fixture() {
  static const ResolvedWorldgenFixture fixture = make_resolved_worldgen_fixture();
  return fixture;
}

[[nodiscard]] arv::world::ChunkGenerationRequest make_resolved_request(
    const ResolvedWorldgenFixture& fixture,
    arv::ChunkCoord coord,
    arv::world::worldgen::WorldGenerationStats* stats = nullptr) {
  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = 0x5EED1234ULL;
  request.coord = coord;
  request.min_chunk_y = arv::floor_div(fixture.config.min_y, arv::ChunkSize);
  request.max_chunk_y = arv::floor_div(fixture.config.max_y, arv::ChunkSize);
  request.config = fixture.config;
  request.resolved_dimension = fixture.dimension;
  request.stats = stats;
  return request;
}

void add_worldgen_stats_counters(
    benchmark::State& state,
    const arv::world::worldgen::WorldGenerationStats& stats,
    arv::u64 chunks,
    std::span<const arv::u64> chunk_latencies_ns = {}) {
  if (chunks == 0U) {
    return;
  }
  const double inv_chunks = 1.0 / static_cast<double>(chunks);
  state.counters["chunks/sec"] =
      benchmark::Counter(static_cast<double>(chunks), benchmark::Counter::kIsRate);
  state.counters["sections/sec"] = benchmark::Counter(
      static_cast<double>(stats.sections.generated_sections),
      benchmark::Counter::kIsRate);
  state.counters["ms/chunk"] =
      static_cast<double>(stats.total_ns) / 1000000.0 * inv_chunks;
  if (!chunk_latencies_ns.empty()) {
    std::vector<arv::u64> sorted{chunk_latencies_ns.begin(), chunk_latencies_ns.end()};
    std::sort(sorted.begin(), sorted.end());
    const auto percentile_ms = [&sorted](arv::usize percentile) {
      const arv::usize rank =
          (sorted.size() - 1U) * percentile / 100U;
      return static_cast<double>(sorted[rank]) / 1000000.0;
    };
    state.counters["p50_ms/chunk"] = percentile_ms(50);
    state.counters["p90_ms/chunk"] = percentile_ms(90);
    state.counters["p99_ms/chunk"] = percentile_ms(99);
  }
  state.counters["sections/chunk"] =
      static_cast<double>(stats.sections.generated_sections) * inv_chunks;
  state.counters["skipped_sections/chunk"] =
      static_cast<double>(stats.sections.skipped_sections) * inv_chunks;
  state.counters["bulk_solid/chunk"] =
      static_cast<double>(stats.sections.bulk_solid_sections) * inv_chunks;
  state.counters["bulk_fluid/chunk"] =
      static_cast<double>(stats.sections.bulk_fluid_sections) * inv_chunks;
  state.counters["all_air_sections/chunk"] =
      static_cast<double>(stats.sections.all_air_sections) * inv_chunks;
  state.counters["mixed_sections/chunk"] =
      static_cast<double>(stats.sections.mixed_sections) * inv_chunks;
  state.counters["placed_features/chunk"] =
      static_cast<double>(stats.candidates.placed_features_considered) * inv_chunks;
  state.counters["feature_origin_chunks/chunk"] =
      static_cast<double>(stats.candidates.feature_origin_chunks_considered) * inv_chunks;
  state.counters["feature_candidates/chunk"] =
      static_cast<double>(stats.candidates.feature_candidates_considered) * inv_chunks;
  state.counters["feature_rejects/chunk"] =
      static_cast<double>(stats.candidates.feature_candidates_rejected) * inv_chunks;
  state.counters["feature_applied/chunk"] =
      static_cast<double>(stats.candidates.feature_candidates_applied) * inv_chunks;
  state.counters["structure_cells/chunk"] =
      static_cast<double>(stats.candidates.structure_cells_inspected) * inv_chunks;
  state.counters["structures/chunk"] =
      static_cast<double>(stats.candidates.structure_candidates_applied) * inv_chunks;
  state.counters["terrain_lattice/chunk"] =
      static_cast<double>(stats.noise.terrain_lattice_samples) * inv_chunks;
  state.counters["climate_lattice/chunk"] =
      static_cast<double>(stats.noise.climate_lattice_samples) * inv_chunks;
  state.counters["direct_2d_noise/chunk"] =
      static_cast<double>(stats.noise.direct_2d_noise_samples) * inv_chunks;
  state.counters["direct_3d_noise/chunk"] =
      static_cast<double>(stats.noise.direct_3d_noise_samples) * inv_chunks;
  for (arv::usize index = 0;
       index < static_cast<arv::usize>(arv::world::worldgen::WorldGenerationStage::Count);
       ++index) {
    state.counters[std::string{
        arv::world::worldgen::WorldGenerationStageNames[index]} + "_ms/chunk"] =
        static_cast<double>(stats.stage_ns[index]) / 1000000.0 * inv_chunks;
  }
}

void accumulate_worldgen_stats(
    arv::world::worldgen::WorldGenerationStats& total,
    const arv::world::worldgen::WorldGenerationStats& next) {
  total.total_ns += next.total_ns;
  for (arv::usize index = 0;
       index < static_cast<arv::usize>(arv::world::worldgen::WorldGenerationStage::Count);
       ++index) {
    total.stage_ns[index] += next.stage_ns[index];
  }
  total.sections.generated_sections += next.sections.generated_sections;
  total.sections.skipped_sections += next.sections.skipped_sections;
  total.sections.all_air_sections += next.sections.all_air_sections;
  total.sections.bulk_solid_sections += next.sections.bulk_solid_sections;
  total.sections.bulk_fluid_sections += next.sections.bulk_fluid_sections;
  total.sections.mixed_sections += next.sections.mixed_sections;
  total.candidates.placed_features_considered += next.candidates.placed_features_considered;
  total.candidates.feature_origin_chunks_considered += next.candidates.feature_origin_chunks_considered;
  total.candidates.feature_candidates_considered += next.candidates.feature_candidates_considered;
  total.candidates.feature_candidates_rejected += next.candidates.feature_candidates_rejected;
  total.candidates.feature_candidates_applied += next.candidates.feature_candidates_applied;
  total.candidates.structure_cells_inspected += next.candidates.structure_cells_inspected;
  total.candidates.structure_candidates_applied += next.candidates.structure_candidates_applied;
  total.noise.terrain_lattice_samples += next.noise.terrain_lattice_samples;
  total.noise.climate_lattice_samples += next.noise.climate_lattice_samples;
  total.noise.direct_2d_noise_samples += next.noise.direct_2d_noise_samples;
  total.noise.direct_3d_noise_samples += next.noise.direct_3d_noise_samples;
}

void run_resolved_chunk_set(
    benchmark::State& state,
    std::span<const arv::ChunkCoord> coords) {
  const ResolvedWorldgenFixture& fixture = resolved_worldgen_fixture();
  arv::world::worldgen::WorldGenerationStats stats;
  std::vector<arv::u64> chunk_latencies_ns;
  arv::u64 chunks = 0;
  for (auto _ : state) {
    for (const arv::ChunkCoord coord : coords) {
      auto request = make_resolved_request(fixture, coord, &stats);
      const arv::u64 before_total_ns = stats.total_ns;
      auto generated = arv::world::generate_chunk(request);
      if (!generated) {
        state.SkipWithError(generated.error().message.c_str());
        break;
      }
      chunk_latencies_ns.push_back(stats.total_ns - before_total_ns);
      benchmark::DoNotOptimize(generated.value().sections.data());
      benchmark::DoNotOptimize(generated.value().surface_y_by_column.data());
      ++chunks;
    }
  }
  add_worldgen_stats_counters(state, stats, chunks, chunk_latencies_ns);
}

[[nodiscard]] arv::world::ChunkGenerationRequest make_variant_request(
    const WorldgenVariantFixture& fixture,
    arv::ChunkCoord coord,
    arv::world::worldgen::WorldGenerationStats* stats = nullptr) {
  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = 0x5EED1234ULL;
  request.coord = coord;
  request.min_chunk_y = arv::floor_div(fixture.config.min_y, arv::ChunkSize);
  request.max_chunk_y = arv::floor_div(fixture.config.max_y, arv::ChunkSize);
  request.config = fixture.config;
  request.resolved_dimension = fixture.dimension;
  request.stats = stats;
  return request;
}

void run_variant_chunk_set(
    benchmark::State& state,
    const WorldgenVariantFixture& fixture) {
  arv::world::worldgen::WorldGenerationStats stats;
  std::vector<arv::u64> chunk_latencies_ns;
  arv::u64 chunks = 0;
  for (auto _ : state) {
    for (const arv::ChunkCoord coord : fixture.coords) {
      auto request = make_variant_request(fixture, coord, &stats);
      const arv::u64 before_total_ns = stats.total_ns;
      auto generated = arv::world::generate_chunk(request);
      if (!generated) {
        state.SkipWithError(generated.error().message.c_str());
        break;
      }
      chunk_latencies_ns.push_back(stats.total_ns - before_total_ns);
      benchmark::DoNotOptimize(generated.value().sections.data());
      ++chunks;
    }
  }
  add_worldgen_stats_counters(state, stats, chunks, chunk_latencies_ns);
}

[[nodiscard]] const WorldgenVariantFixture& structure_heavy_fixture() {
  static const WorldgenVariantFixture fixture = [] {
    const ResolvedWorldgenFixture& base = resolved_worldgen_fixture();
    auto dimension =
        std::make_shared<arv::world::worldgen::ResolvedDimension>(*base.dimension);
    for (arv::world::worldgen::ResolvedStructureSet& set : dimension->structure_sets) {
      set.raw.placement.average_distance_chunks = 1;
      set.raw.placement.separation_chunks = 0;
    }
    arv::world::WorldGenerationConfig config =
        arv::world::config_from_resolved_dimension(dimension);
    return WorldgenVariantFixture{
        std::move(dimension),
        std::move(config),
        make_region_coords(8)};
  }();
  return fixture;
}

[[nodiscard]] const WorldgenVariantFixture& cave_heavy_fixture() {
  static const WorldgenVariantFixture fixture = [] {
    const ResolvedWorldgenFixture& base = resolved_worldgen_fixture();
    auto dimension =
        std::make_shared<arv::world::worldgen::ResolvedDimension>(*base.dimension);
    for (arv::world::worldgen::CarverConfig& carver : dimension->carvers) {
      if (carver.carver_type == arv::world::worldgen::CarverType::NoiseCaves) {
        carver.enabled = true;
        carver.threshold = 0.55F;
      }
    }
    arv::world::WorldGenerationConfig config =
        arv::world::config_from_resolved_dimension(dimension);
    return WorldgenVariantFixture{
        std::move(dimension),
        std::move(config),
        make_region_coords(8)};
  }();
  return fixture;
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

void BM_WorldgenResolvedSingleChunk(benchmark::State& state) {
  const ResolvedWorldgenFixture& fixture = resolved_worldgen_fixture();
  const std::array<arv::ChunkCoord, 1> coords{{fixture.plains}};
  run_resolved_chunk_set(state, coords);
}

void BM_WorldgenResolvedRollingHills(benchmark::State& state) {
  const ResolvedWorldgenFixture& fixture = resolved_worldgen_fixture();
  const std::array<arv::ChunkCoord, 1> coords{{fixture.rolling}};
  run_resolved_chunk_set(state, coords);
}

void BM_WorldgenResolvedMountains(benchmark::State& state) {
  const ResolvedWorldgenFixture& fixture = resolved_worldgen_fixture();
  const std::array<arv::ChunkCoord, 1> coords{{fixture.mountains}};
  run_resolved_chunk_set(state, coords);
}

void BM_WorldgenResolvedOcean(benchmark::State& state) {
  const ResolvedWorldgenFixture& fixture = resolved_worldgen_fixture();
  const std::array<arv::ChunkCoord, 1> coords{{fixture.ocean}};
  run_resolved_chunk_set(state, coords);
}

void BM_WorldgenResolvedFeatureHeavyForest(benchmark::State& state) {
  const ResolvedWorldgenFixture& fixture = resolved_worldgen_fixture();
  const std::array<arv::ChunkCoord, 1> coords{{fixture.forest}};
  run_resolved_chunk_set(state, coords);
}

void BM_WorldgenResolvedBiomeBorder(benchmark::State& state) {
  const ResolvedWorldgenFixture& fixture = resolved_worldgen_fixture();
  const std::array<arv::ChunkCoord, 1> coords{{fixture.biome_border}};
  run_resolved_chunk_set(state, coords);
}

void BM_WorldgenResolvedRegion16(benchmark::State& state) {
  run_resolved_chunk_set(state, resolved_worldgen_fixture().region16);
}

void BM_WorldgenResolvedRegion32(benchmark::State& state) {
  run_resolved_chunk_set(state, resolved_worldgen_fixture().region32);
}

void BM_WorldgenResolvedSpiral(benchmark::State& state) {
  run_resolved_chunk_set(state, resolved_worldgen_fixture().spiral);
}

void BM_WorldgenResolvedCaveHeavyRegion8(benchmark::State& state) {
  run_variant_chunk_set(state, cave_heavy_fixture());
}

void BM_WorldgenResolvedStructureHeavyRegion8(benchmark::State& state) {
  run_variant_chunk_set(state, structure_heavy_fixture());
}

void BM_WorldgenTerrainProfileOnly(benchmark::State& state) {
  const ResolvedWorldgenFixture& fixture = resolved_worldgen_fixture();
  arv::world::worldgen::WorldGenerationStats stats;
  arv::u64 chunks = 0;
  for (auto _ : state) {
    for (const arv::ChunkCoord coord : fixture.region16) {
      const auto start = BenchmarkClock::now();
      const auto profile =
          arv::world::worldgen::generate_terrain_profile(
              *fixture.dimension,
              0x5EED1234ULL,
              coord,
              &stats);
      stats.total_ns += elapsed_ns(start);
      benchmark::DoNotOptimize(profile.surface_y_by_column.data());
      ++chunks;
    }
  }
  state.counters["chunks/sec"] =
      benchmark::Counter(static_cast<double>(chunks), benchmark::Counter::kIsRate);
  state.counters["ms/profile"] =
      chunks == 0U ? 0.0 : static_cast<double>(stats.total_ns) / 1000000.0 /
                            static_cast<double>(chunks);
  state.counters["terrain_lattice/profile"] =
      chunks == 0U ? 0.0 : static_cast<double>(stats.noise.terrain_lattice_samples) /
                            static_cast<double>(chunks);
  state.counters["climate_lattice/profile"] =
      chunks == 0U ? 0.0 : static_cast<double>(stats.noise.climate_lattice_samples) /
                            static_cast<double>(chunks);
  state.counters["direct_2d_noise/profile"] =
      chunks == 0U ? 0.0 : static_cast<double>(stats.noise.direct_2d_noise_samples) /
                            static_cast<double>(chunks);
}

void BM_WorldgenResolvedRegion16JobSystem(benchmark::State& state) {
  const ResolvedWorldgenFixture& fixture = resolved_worldgen_fixture();
  arv::sched::JobSystemConfig config;
  config.critical_cpu_workers = 1;
  config.background_cpu_workers = 4;
  config.blocking_io_workers = 1;
  config.max_chunk_gen_in_flight = 0;
  arv::sched::JobSystem jobs{config};
  jobs.start();
  jobs.reset_pressure_stats();

  arv::u64 chunks = 0;
  arv::u64 queue_wait_ns = 0;
  arv::u64 run_ns = 0;
  arv::world::worldgen::WorldGenerationStats stats;
  std::vector<arv::u64> chunk_latencies_ns;
  for (auto _ : state) {
    std::vector<arv::sched::JobFuture<WorldgenJobBenchmarkResult>> futures;
    futures.reserve(fixture.region16.size());
    for (const arv::ChunkCoord coord : fixture.region16) {
      arv::sched::JobDesc desc;
      desc.name = "benchmark_worldgen_region16";
      desc.lane = arv::sched::JobLane::BackgroundCpu;
      desc.domain = arv::sched::JobDomain::ChunkGeneration;
      auto request = make_resolved_request(fixture, coord);
      const BenchmarkClock::time_point submitted_at = BenchmarkClock::now();
      auto future = jobs.submit_result(
          std::move(desc),
          [request = std::move(request), submitted_at]() mutable {
            const BenchmarkClock::time_point started_at = BenchmarkClock::now();
            arv::world::worldgen::WorldGenerationStats local_stats;
            request.stats = &local_stats;
            auto generated = arv::world::generate_chunk(request);
            const BenchmarkClock::time_point completed_at = BenchmarkClock::now();
            return WorldgenJobBenchmarkResult{
                std::move(generated),
                local_stats,
                elapsed_ns(submitted_at, started_at),
                elapsed_ns(started_at, completed_at)};
          });
      if (!future.valid()) {
        state.SkipWithError("worldgen benchmark job was rejected");
        break;
      }
      futures.push_back(std::move(future));
    }
    for (auto& future : futures) {
      WorldgenJobBenchmarkResult result = future.get();
      if (!result.generated) {
        state.SkipWithError(result.generated.error().message.c_str());
        break;
      }
      queue_wait_ns += result.queue_wait_ns;
      run_ns += result.run_ns;
      chunk_latencies_ns.push_back(result.run_ns);
      accumulate_worldgen_stats(stats, result.stats);
      benchmark::DoNotOptimize(result.generated.value().sections.data());
      ++chunks;
    }
  }

  const arv::sched::JobSystemPressureSnapshot pressure = jobs.pressure_snapshot();
  jobs.request_stop();
  jobs.join();
  add_worldgen_stats_counters(state, stats, chunks, chunk_latencies_ns);
  if (chunks != 0U) {
    const double inv_chunks = 1.0 / static_cast<double>(chunks);
    state.counters["queue_wait_ms/chunk"] =
        static_cast<double>(queue_wait_ns) / 1000000.0 * inv_chunks;
    state.counters["job_run_ms/chunk"] =
        static_cast<double>(run_ns) / 1000000.0 * inv_chunks;
  }
  state.counters["job_peak_queue_depth"] =
      static_cast<double>(pressure.peak_queued_jobs);
  state.counters["job_completed_total"] =
      static_cast<double>(pressure.completed_jobs_total);
}

} // namespace

BENCHMARK(BM_ChunkAllocationReset);
BENCHMARK(BM_BlockGetSet);
BENCHMARK(BM_SectionSnapshotCreation);
BENCHMARK(BM_CopyOnWriteSnapshotPath);
BENCHMARK(BM_NeighborBoundaryExtraction);
BENCHMARK(BM_GenerateChunkColumn);
BENCHMARK(BM_WorldgenResolvedSingleChunk)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_WorldgenResolvedRollingHills)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_WorldgenResolvedMountains)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_WorldgenResolvedOcean)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_WorldgenResolvedFeatureHeavyForest)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_WorldgenResolvedBiomeBorder)->Unit(benchmark::kMicrosecond);
BENCHMARK(BM_WorldgenResolvedRegion16)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WorldgenResolvedRegion32)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WorldgenResolvedSpiral)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WorldgenResolvedCaveHeavyRegion8)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WorldgenResolvedStructureHeavyRegion8)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WorldgenTerrainProfileOnly)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_WorldgenResolvedRegion16JobSystem)->Unit(benchmark::kMillisecond)->UseRealTime();
