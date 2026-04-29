#include "arv/sched/scheduler.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

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
    arv::ChunkCoord coord = {{0, 0, 0}}) {
  arv::world::ChunkGenerationRequest request;
  request.world_id = arv::WorldID{1};
  request.seed = seed;
  request.coord = coord;
  request.config = make_worldgen_config();

  auto palette = arv::world::resolve_world_generation_palette(request.config, registry);
  EXPECT_TRUE(palette);
  request.palette = std::move(palette.value());
  return request;
}

} // namespace

TEST(WorldGenerationTests, SameSeedAndChunkProduceSameOutputThroughAJob) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  const arv::world::ChunkGenerationRequest request = make_request(registry, 1234);

  arv::sched::Scheduler scheduler(1);
  auto first_job = scheduler.submit("generate_first", [request] {
    return arv::world::generate_chunk(request);
  });
  auto second_job = scheduler.submit("generate_second", [request] {
    return arv::world::generate_chunk(request);
  });

  auto first = first_job.get();
  auto second = second_job.get();

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_EQ(first.value().blocks, second.value().blocks);
}

TEST(WorldGenerationTests, DifferentSeedsCanChangeChunkOutput) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();

  auto first = arv::world::generate_chunk(make_request(registry, 1));
  auto second = arv::world::generate_chunk(make_request(registry, 2));

  ASSERT_TRUE(first);
  ASSERT_TRUE(second);
  EXPECT_NE(first.value().blocks, second.value().blocks);
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

TEST(WorldGenerationTests, GeneratedBlocksAreValidRegistryVariants) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto generated = arv::world::generate_chunk(make_request(registry, 99, {{-1, 0, 1}}));

  ASSERT_TRUE(generated);
  for (const arv::voxel::BlockVariantID variant : generated.value().blocks) {
    EXPECT_LT(variant.value, registry.variant_count());
  }
}

TEST(WorldGenerationTests, WorldPublishesGeneratedChunksOnlyForItsWorldId) {
  const arv::voxel::BlockRegistry registry = make_worldgen_registry();
  auto generated = arv::world::generate_chunk(make_request(registry, 99));
  ASSERT_TRUE(generated);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.request_chunk_load({{0, 0, 0}});
  EXPECT_TRUE(world.publish_generated_chunk(generated.value()));
  const arv::voxel::ChunkSection* chunk = world.find_chunk({{0, 0, 0}});
  ASSERT_NE(chunk, nullptr);
  EXPECT_EQ(chunk->lifecycle_state(), arv::voxel::ChunkLifecycleState::Loaded);

  generated.value().world_id = arv::WorldID{2};
  EXPECT_FALSE(world.publish_generated_chunk(generated.value()));
}
