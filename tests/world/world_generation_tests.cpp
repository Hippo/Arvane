#include "arv/sched/job_system.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <utility>
#include <vector>

namespace {

arv::voxel::BlockRegistry make_worldgen_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:dirt"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:grass_block"));
  definitions.push_back(arv::voxel::make_full_cube_definition("arvane:stone"));

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result);
  return std::move(result.value());
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
