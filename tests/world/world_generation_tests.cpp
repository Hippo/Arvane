#include "arv/sched/job_system.hpp"
#include "arv/asset/pack.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"
#include "arv/world/worldgen/chunk_generator.hpp"
#include "arv/world/worldgen/loader.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

arv::voxel::BlockRegistry make_worldgen_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:dirt"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:grass_block"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:sand"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:iron_ore"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:oak_log"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:oak_leaves"));
  arv::voxel::BlockDefinition short_grass =
      arv::voxel::make_full_cube_definition("arvane:short_grass");
  short_grass.properties.replaceable = true;
  definitions.push_back(std::move(short_grass));
  arv::voxel::BlockDefinition water;
  water.name = "arvane:water";
  water.render_category = arv::voxel::RenderCategory::Transparent;
  water.is_item = false;
  water.properties.replaceable = true;
  water.properties.fluid = true;
  definitions.push_back(std::move(water));

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result);
  return std::move(result.value());
}

arv::world::WorldGenerationConfig make_pack_worldgen_config(
    const arv::voxel::BlockRegistry& registry) {
  auto packs = arv::asset::PackManager::load_from_packs_root("arvane_home/packs");
  EXPECT_TRUE(packs) << packs.error().describe();
  auto worldgen_registry =
      arv::world::worldgen::load_worldgen_registry_from_packs(packs.value());
  EXPECT_TRUE(worldgen_registry) << worldgen_registry.error().describe();
  auto resolved = arv::world::worldgen::resolve_dimension(
      worldgen_registry.value(),
      "arvane:worldgen/dimension/overworld",
      registry);
  EXPECT_TRUE(resolved) << resolved.error().describe();
  return arv::world::config_from_resolved_dimension(std::move(resolved.value()));
}

arv::world::worldgen::ResolvedDimension make_two_biome_blend_dimension() {
  namespace worldgen = arv::world::worldgen;

  worldgen::ResolvedDimension dimension;
  dimension.raw.id = "arvane:test/two_biome_blend";
  dimension.raw.version = 1;
  dimension.raw.seed_salt = "two_biome_blend";
  dimension.raw.vertical.min_y = -32;
  dimension.raw.vertical.max_y = 96;
  dimension.raw.vertical.sea_level = 0;
  dimension.terrain.height.base = 32;
  dimension.terrain.height.scale = 0;
  dimension.terrain.height.detail_scale = 0;
  dimension.terrain.primary_noise.seed_salt = "flat_primary";
  dimension.terrain.primary_noise.frequency = 0.01F;
  dimension.terrain.primary_noise.amplitude = 0.0F;
  dimension.terrain.detail_noise.seed_salt = "flat_detail";
  dimension.terrain.detail_noise.frequency = 0.01F;
  dimension.terrain.detail_noise.amplitude = 0.0F;

  worldgen::BiomeConfig low;
  low.id = "arvane:worldgen/biome/test_low";
  low.terrain.base_height_offset = -16.0F;
  low.terrain.height_scale_multiplier = 1.0F;
  low.terrain.roughness_multiplier = 1.0F;
  worldgen::BiomeConfig high;
  high.id = "arvane:worldgen/biome/test_high";
  high.terrain.base_height_offset = 16.0F;
  high.terrain.height_scale_multiplier = 1.0F;
  high.terrain.roughness_multiplier = 1.0F;
  dimension.biomes.push_back({std::move(low), std::nullopt});
  dimension.biomes.push_back({std::move(high), std::nullopt});

  worldgen::NoiseFieldConfig climate;
  climate.type = worldgen::NoiseType::Simplex2D;
  climate.type_id = "arvane:noise/simplex_2d";
  climate.seed_salt = "blend_temperature";
  climate.frequency = 0.025F;
  climate.octaves = 1;
  climate.amplitude = 1.0F;
  climate.lacunarity = 2.0F;
  climate.gain = 0.5F;
  dimension.biome_source.raw.id = "arvane:test/biome_source/two_biome_blend";
  dimension.biome_source.raw.sample_scale_xz = 4;
  dimension.biome_source.fields.push_back({"temperature", climate});

  worldgen::ResolvedBiomeSourceEntry low_entry;
  low_entry.biome_index = 0;
  low_entry.climate.push_back({"temperature", {-1.0F, 0.0F}});
  low_entry.resolved_climate.push_back({0U, {-1.0F, 0.0F}});
  worldgen::ResolvedBiomeSourceEntry high_entry;
  high_entry.biome_index = 1;
  high_entry.climate.push_back({"temperature", {0.0F, 1.0F}});
  high_entry.resolved_climate.push_back({0U, {0.0F, 1.0F}});
  dimension.biome_source.entries.push_back(std::move(low_entry));
  dimension.biome_source.entries.push_back(std::move(high_entry));
  return dimension;
}

arv::world::WorldGenerationConfig make_worldgen_config() {
  auto parsed = arv::world::parse_world_generation_yaml(R"yaml(
id: arvane:test_overworld
version: 1
seed_salt: "test"
vertical:
  min_y: -16
  max_y: 64
  sea_level: 16
noise:
  terrain:
    frequency: 0.1
density:
  base_height: 8
  height_scale: 12
surface:
  rules:
    - layers:
        - block: arvane:grass_block
          depth: 1
        - block: arvane:dirt
          depth: 2
        - block: arvane:stone
          depth: remaining
)yaml");
  EXPECT_TRUE(parsed);
  return std::move(parsed.value());
}

arv::world::ChunkGenerationRequest make_request(
    const arv::voxel::BlockRegistry& registry,
    arv::u64 seed,
    arv::ChunkSectionCoord coord = {{0, 0, 0}}) {
  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = seed;
  request.coord = arv::chunk_to_column(coord);
  request.config = make_worldgen_config();
  request.min_chunk_y = arv::floor_div(request.config.min_y, arv::ChunkSize);
  request.max_chunk_y = arv::floor_div(request.config.max_y, arv::ChunkSize);

  auto palette = arv::world::resolve_world_generation_palette(request.config, registry);
  EXPECT_TRUE(palette);
  request.palette = std::move(palette.value());
  return request;
}

} // namespace

TEST(WorldGenerationTests, SameSeedAndChunkProduceSameOutputThroughAJob) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::ChunkGenerationRequest request = make_request(registry, 1234);

  arv::sched::JobSystemConfig job_config;
  job_config.critical_cpu_workers = 1;
  job_config.background_cpu_workers = 1;
  job_config.blocking_io_workers = 1;
  arv::sched::JobSystem jobs{job_config};
  jobs.start();
  arv::sched::JobDesc first_desc;
  first_desc.name = "generate_first";
  first_desc.lane = arv::sched::JobLane::BackgroundCpu;
  first_desc.domain = arv::sched::JobDomain::ChunkGeneration;
  auto first_job = jobs.submit_result(std::move(first_desc), [request] {
    return arv::world::generate_chunk(request);
  });
  arv::sched::JobDesc second_desc;
  second_desc.name = "generate_second";
  second_desc.lane = arv::sched::JobLane::BackgroundCpu;
  second_desc.domain = arv::sched::JobDomain::ChunkGeneration;
  auto second_job = jobs.submit_result(std::move(second_desc), [request] {
    return arv::world::generate_chunk(request);
  });

  auto first = first_job.get();
  auto second = second_job.get();

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(first.value().sections, second.value().sections);
}

TEST(WorldGenerationTests, LoadsPackWorldgenResourcesAndResolvesOverworld) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);

  ASSERT_NE(config.resolved_dimension, nullptr);
  EXPECT_EQ(config.id, "arvane:worldgen/dimension/overworld");
  EXPECT_EQ(config.min_y, -32);
  EXPECT_EQ(config.max_y, 128);
  EXPECT_GE(config.resolved_dimension->biomes.size(), 7U);
  EXPECT_GE(config.resolved_dimension->surface_rule_sets.size(), 3U);
  EXPECT_FALSE(config.resolved_dimension->carvers.empty());
  EXPECT_FALSE(config.resolved_dimension->placed_features.empty());
  EXPECT_FALSE(config.resolved_dimension->structure_sets.empty());
}

TEST(WorldGenerationTests, PackWorldgenIsDeterministicAndBiomeBacked) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  auto palette = arv::world::resolve_world_generation_palette(config, registry);
  ASSERT_TRUE(palette) << palette.error().message;

  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = 8181;
  request.coord = {2, -3};
  request.min_chunk_y = arv::floor_div(config.min_y, arv::ChunkSize);
  request.max_chunk_y = arv::floor_div(config.max_y, arv::ChunkSize);
  request.config = config;
  request.palette = std::move(palette.value());

  auto first = arv::world::generate_chunk(request);
  auto second = arv::world::generate_chunk(request);
  ASSERT_TRUE(first) << first.error().message;
  ASSERT_TRUE(second) << second.error().message;
  EXPECT_EQ(first.value().sections, second.value().sections);

  const arv::world::worldgen::TerrainProfile profile =
      arv::world::worldgen::generate_terrain_profile(
          *config.resolved_dimension,
          request.seed,
          request.coord);
  bool saw_biome = false;
  for (const arv::usize biome_index : profile.biome_by_column) {
    saw_biome = saw_biome || biome_index < config.resolved_dimension->biomes.size();
  }
  EXPECT_TRUE(saw_biome);
}

TEST(WorldGenerationTests, PackOverworldScanFindsMultipleBiomeFamilies) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);

  struct ScanCase {
    arv::u64 seed{};
    arv::ChunkCoord center{};
    arv::usize min_biomes{};
    bool require_desert{};
    bool require_grassy{};
    bool require_mountain{};
    bool require_ocean_or_coast{};
  };
  const std::array<ScanCase, 2> scan_cases{
      ScanCase{7390150598859ULL, {0, 0}, 4U, true, true, true, true},
      ScanCase{32187121430964ULL, {-3, 141}, 3U, false, true, true, true},
  };

  for (const ScanCase& scan : scan_cases) {
    std::unordered_set<std::string> seen;
    bool saw_desert = false;
    bool saw_grassy = false;
    bool saw_mountain = false;
    bool saw_ocean_or_coast = false;
    for (arv::i32 z = scan.center.z - 24; z <= scan.center.z + 24; z += 3) {
      for (arv::i32 x = scan.center.x - 24; x <= scan.center.x + 24; x += 3) {
        const arv::world::worldgen::TerrainProfile profile =
            arv::world::worldgen::generate_terrain_profile(
                *config.resolved_dimension,
                scan.seed,
                {x, z});
        for (const arv::usize biome_index : profile.biome_by_column) {
          ASSERT_LT(biome_index, config.resolved_dimension->biomes.size());
          const std::string& id = config.resolved_dimension->biomes[biome_index].raw.id;
          seen.insert(id);
          saw_desert = saw_desert || id == "arvane:worldgen/biome/desert";
          saw_grassy = saw_grassy || id == "arvane:worldgen/biome/plains" ||
                       id == "arvane:worldgen/biome/forest";
          saw_mountain = saw_mountain || id == "arvane:worldgen/biome/mountains";
          saw_ocean_or_coast = saw_ocean_or_coast ||
                               id == "arvane:worldgen/biome/beach" ||
                               id == "arvane:worldgen/biome/ocean" ||
                               id == "arvane:worldgen/biome/deep_ocean";
        }
      }
    }

    std::string seen_text;
    for (const std::string& id : seen) {
      if (!seen_text.empty()) {
        seen_text += ", ";
      }
      seen_text += id;
    }
    EXPECT_GE(seen.size(), scan.min_biomes) << "seed=" << scan.seed << " center=("
                                            << scan.center.x << "," << scan.center.z
                                            << ") seen=" << seen_text;
    if (scan.require_desert) {
      EXPECT_TRUE(saw_desert) << "seed=" << scan.seed << " seen=" << seen_text;
    }
    if (scan.require_grassy) {
      EXPECT_TRUE(saw_grassy) << "seed=" << scan.seed << " seen=" << seen_text;
    }
    if (scan.require_mountain) {
      EXPECT_TRUE(saw_mountain) << "seed=" << scan.seed << " seen=" << seen_text;
    }
    if (scan.require_ocean_or_coast) {
      EXPECT_TRUE(saw_ocean_or_coast) << "seed=" << scan.seed << " seen=" << seen_text;
    }
  }
}

TEST(WorldGenerationTests, BiomeBlendWeightsNormalizeAndIncludeNeighbors) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::WorldGenerationConfig config = make_pack_worldgen_config(registry);
  ASSERT_NE(config.resolved_dimension, nullptr);

  bool saw_neighbor_weight = false;
  for (arv::i32 z = -12; z <= 12; z += 2) {
    for (arv::i32 x = -12; x <= 12; x += 2) {
      const arv::world::worldgen::TerrainProfile profile =
          arv::world::worldgen::generate_terrain_profile(
              *config.resolved_dimension,
              7390150598859ULL,
              {x, z});
      for (const arv::world::worldgen::BiomeSample& sample :
           profile.biome_samples_by_column) {
        ASSERT_GT(sample.weight_count, 0U);
        ASSERT_LE(sample.weight_count, arv::world::worldgen::BiomeBlendWeightCount);
        arv::f32 total = 0.0F;
        for (arv::usize index = 0; index < sample.weight_count; ++index) {
          ASSERT_LT(sample.weights[index].biome_index, config.resolved_dimension->biomes.size());
          ASSERT_GE(sample.weights[index].weight, 0.0F);
          total += sample.weights[index].weight;
          saw_neighbor_weight =
              saw_neighbor_weight ||
              (sample.weights[index].biome_index != sample.dominant_biome &&
               sample.weights[index].weight > 0.05F);
        }
        EXPECT_NEAR(total, 1.0F, 0.001F);
      }
    }
  }

  EXPECT_TRUE(saw_neighbor_weight);
}

TEST(WorldGenerationTests, BlendedBiomeTerrainAvoidsHardBoundaryCliffs) {
  arv::world::worldgen::ResolvedDimension dimension = make_two_biome_blend_dimension();

  bool found_boundary = false;
  for (arv::i32 chunk_z = -16; chunk_z <= 16 && !found_boundary; ++chunk_z) {
    for (arv::i32 chunk_x = -16; chunk_x <= 16 && !found_boundary; ++chunk_x) {
      const arv::world::worldgen::TerrainProfile profile =
          arv::world::worldgen::generate_terrain_profile(dimension, 424242ULL, {chunk_x, chunk_z});
      for (arv::i32 z = 0; z < arv::ChunkSize && !found_boundary; ++z) {
        for (arv::i32 x = 0; x < arv::ChunkSize - 1 && !found_boundary; ++x) {
          const arv::usize lhs = arv::voxel::local_column_index({x, z});
          const arv::usize rhs = arv::voxel::local_column_index({x + 1, z});
          if (profile.biome_by_column[lhs] == profile.biome_by_column[rhs]) {
            continue;
          }
          found_boundary = true;
          EXPECT_LE(
              std::abs(profile.surface_y_by_column[lhs] -
                       profile.surface_y_by_column[rhs]),
              8);
          EXPECT_GT(profile.biome_samples_by_column[lhs].weight_count, 1U);
          EXPECT_GT(profile.biome_samples_by_column[rhs].weight_count, 1U);
        }
      }
    }
  }

  EXPECT_TRUE(found_boundary);
}

TEST(WorldGenerationTests, BiomeSampleScaleChangesClimateLookupDeterministically) {
  arv::world::worldgen::ResolvedDimension fine = make_two_biome_blend_dimension();
  arv::world::worldgen::ResolvedDimension coarse = fine;
  fine.biome_source.raw.sample_scale_xz = 1;
  coarse.biome_source.raw.sample_scale_xz = 16;

  bool found_scale_difference = false;
  for (arv::i32 chunk_z = -16; chunk_z <= 16 && !found_scale_difference; ++chunk_z) {
    for (arv::i32 chunk_x = -16; chunk_x <= 16 && !found_scale_difference; ++chunk_x) {
      const arv::ChunkCoord coord{chunk_x, chunk_z};
      const arv::world::worldgen::TerrainProfile first =
          arv::world::worldgen::generate_terrain_profile(fine, 8675309ULL, coord);
      const arv::world::worldgen::TerrainProfile second =
          arv::world::worldgen::generate_terrain_profile(fine, 8675309ULL, coord);
      const arv::world::worldgen::TerrainProfile coarser =
          arv::world::worldgen::generate_terrain_profile(coarse, 8675309ULL, coord);

      EXPECT_EQ(first.biome_by_column, second.biome_by_column);
      EXPECT_EQ(first.surface_y_by_column, second.surface_y_by_column);
      found_scale_difference =
          first.biome_by_column != coarser.biome_by_column ||
          first.surface_y_by_column != coarser.surface_y_by_column;
    }
  }

  EXPECT_TRUE(found_scale_difference);
}

TEST(WorldGenerationTests, DifferentSeedsCanChangeChunkOutput) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();

  auto first = arv::world::generate_chunk(make_request(registry, 1));
  auto second = arv::world::generate_chunk(make_request(registry, 2));

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(first.value().sections, second.value().sections);
}

TEST(WorldGenerationTests, NoiseVariesTerrainHeightAcrossWorldSpace) {
  arv::world::WorldGenerationConfig config = make_worldgen_config();
  config.terrain_frequency = 0.035F;
  config.height_scale = 18;

  const arv::i32 first = arv::world::terrain_height_at(config, 77, 0, 0);
  bool varied = false;
  for (arv::i32 z = 0; z <= 128 && !varied; z += 16) {
    for (arv::i32 x = 0; x <= 128 && !varied; x += 16) {
      varied = arv::world::terrain_height_at(config, 77, x, z) != first;
    }
  }

  EXPECT_TRUE(varied);
}

TEST(WorldGenerationTests, SurfaceProfileMatchesScalarTerrainHeights) {
  arv::world::WorldGenerationConfig config = make_worldgen_config();
  config.terrain_frequency = 0.035F;
  config.height_scale = 18;
  const arv::ChunkCoord coord{-2, 3};

  const arv::world::GenerationSurfaceProfile profile =
      arv::world::terrain_height_profile_at(config, 77, coord);

  for (arv::i32 z = 0; z < arv::ChunkSize; ++z) {
    for (arv::i32 x = 0; x < arv::ChunkSize; ++x) {
      EXPECT_EQ(
          profile[static_cast<arv::usize>(x) +
                  arv::voxel::ChunkSide * static_cast<arv::usize>(z)],
          arv::world::terrain_height_at(
              config,
              77,
              coord.x * arv::ChunkSize + x,
              coord.z * arv::ChunkSize + z));
    }
  }
}

TEST(WorldGenerationTests, GeneratedChunkCarriesSurfaceProfile) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  arv::world::ChunkGenerationRequest request = make_request(registry, 99, {{-1, 0, 1}});

  auto generated = arv::world::generate_chunk(request);

  ASSERT_TRUE(generated);
  EXPECT_EQ(
      generated.value().surface_y_by_column,
      arv::world::terrain_height_profile_at(request.config, request.seed, request.coord));
}

TEST(WorldGenerationTests, GeneratedBlocksAreValidRegistryVariants) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto generated = arv::world::generate_chunk(make_request(registry, 99, {{-1, 0, 1}}));

  ASSERT_TRUE(generated);
  for (const arv::world::GeneratedChunkSection& section : generated.value().sections) {
    for (const arv::voxel::BlockVariantID variant : section.blocks) {
      EXPECT_LT(variant.value, registry.variant_count());
    }
  }
}

TEST(WorldGenerationTests, ColumnGenerationIncludesNegativeConfiguredSections) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  arv::world::ChunkGenerationRequest request = make_request(registry, 99, {{-1, 0, 1}});
  request.min_chunk_y = -2;
  request.max_chunk_y = 1;

  auto generated = arv::world::generate_chunk(request);

  ASSERT_TRUE(generated);
  ASSERT_EQ(generated.value().sections.size(), 4U);
  EXPECT_EQ(generated.value().sections.front().coord, (arv::ChunkSectionCoord{{-1, -2, 1}}));
  EXPECT_EQ(generated.value().sections.back().coord, (arv::ChunkSectionCoord{{-1, 1, 1}}));
}

TEST(WorldGenerationTests, SectionsAboveConfiguredTerrainRemainAir) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  arv::world::ChunkGenerationRequest request = make_request(registry, 99, {{0, 0, 0}});
  request.min_chunk_y = arv::floor_div(request.config.max_y, arv::ChunkSize) + 1;
  request.max_chunk_y = request.min_chunk_y;

  auto generated = arv::world::generate_chunk(request);

  ASSERT_TRUE(generated);
  ASSERT_EQ(generated.value().sections.size(), 1U);
  for (const arv::voxel::BlockVariantID variant : generated.value().sections.front().blocks) {
    EXPECT_EQ(variant, arv::voxel::AirBlockVariantID);
  }
}

TEST(WorldGenerationTests, WorldPublishesGeneratedChunksOnlyForItsWorldId) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto generated = arv::world::generate_chunk(make_request(registry, 99));
  ASSERT_TRUE(generated);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height(
      {
          generated.value().sections.front().coord.value.y,
          generated.value().sections.back().coord.value.y,
      });
  world.request_chunk_load({{0, 0, 0}});
  EXPECT_TRUE(world.publish_generated_chunk(generated.value()));
  const arv::voxel::ChunkSection* chunk = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  EXPECT_EQ(chunk->lifecycle_state(), arv::voxel::ChunkLifecycleState::Loaded);

  generated.value().world_id = arv::WorldID{2};
  EXPECT_FALSE(world.publish_generated_chunk(generated.value()));
}

TEST(WorldGenerationTests, ColumnGenerationPreservesLoadedSavedSections) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto generated = arv::world::generate_chunk(make_request(registry, 99));
  ASSERT_TRUE(generated);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height(
      {
          generated.value().sections.front().coord.value.y,
          generated.value().sections.back().coord.value.y,
      });

  const arv::voxel::BlockVariantID dirt = registry.find_variant("arvane:dirt").value();
  std::array<arv::voxel::BlockVariantID, arv::voxel::ChunkVolume> saved_blocks{};
  saved_blocks.fill(dirt);
  world.apply_chunk_snapshot({{0, 0, 0}}, saved_blocks, std::nullopt);
  world.request_chunk_load(generated.value().sections.back().coord);

  ASSERT_TRUE(world.publish_generated_chunk(generated.value()));

  const arv::voxel::ChunkSection* saved_section = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(saved_section, nullptr);
  EXPECT_EQ(saved_section->lifecycle_state(), arv::voxel::ChunkLifecycleState::Loaded);
  EXPECT_EQ(saved_section->blocks(), saved_blocks);

  const arv::voxel::ChunkSection* filled_missing_section =
      world.find_chunk(generated.value().sections.back().coord);
  ASSERT_NE(filled_missing_section, nullptr);
  EXPECT_EQ(
      filled_missing_section->lifecycle_state(),
      arv::voxel::ChunkLifecycleState::Loaded);
}
