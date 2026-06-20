#include "arv/client/particles.hpp"

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/world/world_queries.hpp"
#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

[[nodiscard]] arv::client::ParticleType make_test_type(std::string name = "arvane:test") {
  arv::client::ParticleType type;
  type.name = std::move(name);
  type.frames = {
      {"arvane:texture/particle/test_0", {0.0F, 0.0F}, {16.0F, 16.0F}, 0U, 16U, 16U},
      {"arvane:texture/particle/test_1", {16.0F, 0.0F}, {32.0F, 16.0F}, 0U, 16U, 16U},
      {"arvane:texture/particle/test_2", {32.0F, 0.0F}, {48.0F, 16.0F}, 0U, 16U, 16U},
      {"arvane:texture/particle/test_3", {48.0F, 0.0F}, {64.0F, 16.0F}, 0U, 16U, 16U},
  };
  type.animation.sample_mode = arv::client::ParticleAnimationSampleMode::LifetimeScaled;
  type.animation.playback = arv::client::ParticleAnimationPlayback::Once;
  type.count = {1U, 1U};
  type.lifetime_seconds = {1.0F, 1.0F};
  type.size_start = {0.1F, 0.1F};
  type.size_end = {0.1F, 0.1F};
  type.color = {{1.0F, 1.0F, 1.0F, 1.0F}, {1.0F, 1.0F, 1.0F, 1.0F}};
  type.distance_scale = true;
  return type;
}

[[nodiscard]] std::filesystem::path test_arvane_home() {
  std::filesystem::path path{ARVANE_TEST_ASSET_ROOT};
  return path.parent_path().parent_path().parent_path().parent_path();
}

[[nodiscard]] arv::voxel::BlockRegistry make_local_particle_block_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  arv::voxel::BlockDefinition stone =
      arv::voxel::make_full_cube_definition("arvane:stone");
  stone.particle_texture_asset = "arvane:texture/block/stone";
  definitions.push_back(std::move(stone));

  auto registry = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(registry);
  return std::move(registry.value());
}

void ensure_loaded_chunk(arv::world::WorldRuntime& world, arv::ChunkSectionCoord coord) {
  (void)world.ensure_chunk(coord);
}

} // namespace

TEST(ParticleSystemTests, SelectsAnimationFramesByLifetimeAndFixedFps) {
  arv::client::ParticleAnimation lifetime;
  lifetime.sample_mode = arv::client::ParticleAnimationSampleMode::LifetimeScaled;
  lifetime.playback = arv::client::ParticleAnimationPlayback::Once;
  EXPECT_EQ(arv::client::particle_animation_frame_index(lifetime, 16, 0.0F, 0.8F), 0U);
  EXPECT_EQ(arv::client::particle_animation_frame_index(lifetime, 16, 0.4F, 0.8F), 8U);
  EXPECT_EQ(arv::client::particle_animation_frame_index(lifetime, 16, 0.799F, 0.8F), 15U);
  EXPECT_EQ(arv::client::particle_animation_frame_index(lifetime, 16, 2.0F, 0.8F), 15U);

  arv::client::ParticleAnimation fixed;
  fixed.sample_mode = arv::client::ParticleAnimationSampleMode::FixedFps;
  fixed.playback = arv::client::ParticleAnimationPlayback::Loop;
  fixed.frames_per_second = 10.0F;
  EXPECT_EQ(arv::client::particle_animation_frame_index(fixed, 4, 0.0F, 1.0F), 0U);
  EXPECT_EQ(arv::client::particle_animation_frame_index(fixed, 4, 0.31F, 1.0F), 3U);
  EXPECT_EQ(arv::client::particle_animation_frame_index(fixed, 4, 0.41F, 1.0F), 0U);
}

TEST(ParticleSystemTests, RejectsInvalidParticleDefinitions) {
  const auto empty_sequence = arv::client::parse_particle_definition_yaml(R"yaml(
id: arvane:bad
texture:
  mode: sequence
  frames: []
)yaml");
  EXPECT_FALSE(empty_sequence);

  const auto bad_fps = arv::client::parse_particle_definition_yaml(R"yaml(
id: arvane:bad
texture:
  mode: static
  image: arvane:texture/particle/test
animation:
  sample: fixed_fps
  fps: 0
)yaml");
  EXPECT_FALSE(bad_fps);
}

TEST(ParticleSystemTests, CollectsUniqueBlockParticleTextureAssets) {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  arv::voxel::BlockDefinition stone =
      arv::voxel::make_full_cube_definition("arvane:stone");
  stone.particle_texture_asset = "arvane:texture/block/stone";
  definitions.push_back(std::move(stone));
  arv::voxel::BlockDefinition grass =
      arv::voxel::make_full_cube_definition("arvane:grass");
  grass.particle_texture_asset = "arvane:texture/block/grass";
  definitions.push_back(std::move(grass));
  arv::voxel::BlockDefinition stone_stairs =
      arv::voxel::make_full_cube_definition("arvane:stone_stairs");
  stone_stairs.particle_texture_asset = "arvane:texture/block/stone";
  definitions.push_back(std::move(stone_stairs));

  auto registry = arv::voxel::BlockRegistry::build(std::move(definitions));
  ASSERT_TRUE(registry);

  const std::vector<std::string> textures =
      arv::client::block_particle_texture_assets(registry.value());

  ASSERT_EQ(textures.size(), 2U);
  EXPECT_NE(
      std::find(
          textures.begin(),
          textures.end(),
          "arvane:texture/block/stone"),
      textures.end());
  EXPECT_NE(
      std::find(
          textures.begin(),
          textures.end(),
          "arvane:texture/block/grass"),
      textures.end());
}

TEST(ParticleSystemTests, BuildsLocalBlockFaceBurstFromRaycastHit) {
  const arv::voxel::BlockRegistry registry = make_local_particle_block_registry();
  const arv::voxel::BlockVariantID stone =
      registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{3, 1, 0}}, stone));

  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::client::LocalParticleSeedState seeds;
  const std::optional<arv::client::ParticleBurstDesc> burst =
      arv::client::build_local_block_face_particle_burst(
          {
              .collision_query = query,
              .block_registry = registry,
              .seed_state = seeds,
              .origin = {0.5F, 1.62F, 0.5F},
              .direction = {1.0F, 0.0F, 0.0F},
              .max_distance = 5.0F,
              .count = 5U,
              .extent = 0.22F,
              .radius = 0.025F,
          });

  ASSERT_TRUE(burst.has_value());
  EXPECT_EQ(burst->type_name, "arvane:block_dust");
  EXPECT_EQ(burst->texture_asset, "arvane:texture/block/stone");
  EXPECT_EQ(burst->normal, (arv::Vec3f{-1.0F, 0.0F, 0.0F}));
  EXPECT_EQ(burst->count, 5U);
  EXPECT_EQ(burst->shape, arv::protocol::ParticleSpawnShape::BlockFace);
  EXPECT_EQ(seeds.next_sequence, 2U);
  arv::client::LocalParticleSeedState expected_seed_state;
  EXPECT_EQ(
      burst->seed,
      arv::client::next_local_particle_seed(
          expected_seed_state,
          arv::BlockPos{{3, 1, 0}}));
  EXPECT_NE(burst->seed, 0U);
}

TEST(ParticleSystemTests, LocalBlockFaceBurstDoesNotConsumeSeedOnMiss) {
  const arv::voxel::BlockRegistry registry = make_local_particle_block_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  const arv::world::WorldCollisionQuery query(world, registry, false);
  arv::client::LocalParticleSeedState seeds;

  const std::optional<arv::client::ParticleBurstDesc> burst =
      arv::client::build_local_block_face_particle_burst(
          {
              .collision_query = query,
              .block_registry = registry,
              .seed_state = seeds,
              .origin = {0.5F, 1.62F, 0.5F},
              .direction = {1.0F, 0.0F, 0.0F},
              .max_distance = 5.0F,
              .count = 5U,
              .extent = 0.22F,
              .radius = 0.025F,
          });

  EXPECT_FALSE(burst.has_value());
  EXPECT_EQ(seeds.next_sequence, 1U);
}

TEST(ParticleSystemTests, LocalBlockMiningParticlesRespectIntervalAndReset) {
  arv::client::LocalBlockMiningParticleState state;

  EXPECT_TRUE(arv::client::consume_local_block_mining_particle_due(state, true, 0.0));
  EXPECT_FALSE(arv::client::consume_local_block_mining_particle_due(state, true, 0.04));
  EXPECT_TRUE(arv::client::consume_local_block_mining_particle_due(state, true, 0.09));
  EXPECT_FALSE(arv::client::consume_local_block_mining_particle_due(state, false, 0.10));
  EXPECT_TRUE(arv::client::consume_local_block_mining_particle_due(state, true, 0.11));
}

TEST(ParticleSystemTests, LocalSprintGroundParticlesRequireForwardGroundedSprint) {
  const arv::voxel::BlockRegistry registry = make_local_particle_block_registry();
  const arv::voxel::BlockVariantID stone =
      registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  ensure_loaded_chunk(world, {{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  const arv::world::WorldCollisionQuery query(world, registry, false);

  arv::client::LocalSprintGroundParticleState state;
  arv::client::LocalParticleSeedState seeds;
  const auto make_input = [&](arv::Seconds delta_seconds) {
    return arv::client::LocalSprintGroundParticleInput{
        .collision_query = query,
        .block_registry = registry,
        .seed_state = seeds,
        .sprint_held = true,
        .move_forward = true,
        .move_backward = false,
        .sneak_held = false,
        .player_grounded = true,
        .player_position = {0.5F, 1.0F, 0.5F},
        .player_velocity = {0.0F, 0.0F, 2.0F},
        .delta_seconds = delta_seconds,
    };
  };

  EXPECT_FALSE(
      arv::client::update_local_sprint_ground_particle_burst(
          state,
          make_input(0.05))
          .has_value());
  std::optional<arv::client::ParticleBurstDesc> burst =
      arv::client::update_local_sprint_ground_particle_burst(
          state,
          make_input(0.07));
  ASSERT_TRUE(burst.has_value());
  EXPECT_EQ(burst->type_name, "arvane:block_dust");
  EXPECT_EQ(burst->texture_asset, "arvane:texture/block/stone");
  EXPECT_EQ(burst->normal, (arv::Vec3f{0.0F, 1.0F, 0.0F}));
  EXPECT_EQ(burst->count, 3U);
  EXPECT_EQ(seeds.next_sequence, 2U);

  auto stopped = make_input(0.20);
  stopped.move_forward = false;
  EXPECT_FALSE(
      arv::client::update_local_sprint_ground_particle_burst(state, stopped)
          .has_value());
  EXPECT_FLOAT_EQ(state.accumulator_seconds, 0.0);
}

TEST(ParticleSystemTests, SimulatesLifetimeAndRendersAnimatedFrames) {
  arv::client::ParticleRegistry registry;
  const arv::client::ParticleTypeID id = registry.add(make_test_type());
  ASSERT_NE(id.value, 0U);

  arv::client::ParticleSystem system;
  system.set_registry(&registry);
  arv::client::ParticleBurstDesc burst;
  burst.type_name = "arvane:test";
  burst.position = {0.0F, 0.0F, 0.0F};
  burst.count = 1U;
  burst.seed = 99U;
  EXPECT_EQ(system.spawn_burst(burst, {0.0F, 0.0F, -2.0F}), 1U);

  std::vector<arv::render::ParticleDrawItem> items;
  system.append_render_items(items);
  ASSERT_EQ(items.size(), 1U);
  EXPECT_EQ(items.front().uv_min, (arv::Vec2f{0.0F, 0.0F}));

  system.update(0.51F);
  items.clear();
  system.append_render_items(items);
  ASSERT_EQ(items.size(), 1U);
  EXPECT_EQ(items.front().uv_min, (arv::Vec2f{32.0F, 0.0F}));

  system.update(0.60F);
  EXPECT_EQ(system.live_count(), 0U);
  EXPECT_EQ(system.stats().expired_total, 1U);
}

TEST(ParticleSystemTests, BurstTextureOverrideUsesResolvedAtlasFrame) {
  arv::client::ParticleRegistry registry;
  (void)registry.add(make_test_type());

  arv::client::ParticleSystem system;
  system.set_registry(&registry);
  arv::client::ParticleBurstDesc burst;
  burst.type_name = "arvane:test";
  burst.texture_asset = "arvane:texture/particle/test_2";
  burst.count = 1U;
  burst.seed = 11U;
  ASSERT_EQ(system.spawn_burst(burst, {0.0F, 0.0F, 0.0F}), 1U);

  std::vector<arv::render::ParticleDrawItem> items;
  system.append_render_items(items);
  ASSERT_EQ(items.size(), 1U);
  EXPECT_EQ(items.front().uv_min, (arv::Vec2f{32.0F, 0.0F}));

  arv::client::ParticleBurstDesc missing = burst;
  missing.texture_asset = "arvane:texture/block/missing";
  EXPECT_EQ(system.spawn_burst(missing, {0.0F, 0.0F, 0.0F}), 0U);
  EXPECT_EQ(system.stats().rejected_invalid, 1U);
}

TEST(ParticleSystemTests, AppliesBudgetAndDistanceRejection) {
  arv::client::ParticleRegistry registry;
  (void)registry.add(make_test_type());

  arv::client::ParticleBudgetConfig budgets;
  budgets.max_live_particles = 1U;
  budgets.max_spawn_per_burst = 8U;
  budgets.cull_distance = 10.0F;
  arv::client::ParticleSystem system{budgets};
  system.set_registry(&registry);

  arv::client::ParticleBurstDesc burst;
  burst.type_name = "arvane:test";
  burst.count = 3U;
  burst.seed = 1U;
  EXPECT_EQ(system.spawn_burst(burst, {0.0F, 0.0F, 0.0F}), 1U);
  EXPECT_EQ(system.stats().rejected_budget, 2U);
  EXPECT_EQ(system.spawn_burst(burst, {0.0F, 0.0F, 0.0F}), 0U);
  EXPECT_GT(system.stats().rejected_budget, 2U);

  arv::client::ParticleSystem distance_system{budgets};
  distance_system.set_registry(&registry);
  burst.position = {20.0F, 0.0F, 0.0F};
  EXPECT_EQ(distance_system.spawn_burst(burst, {0.0F, 0.0F, 0.0F}), 0U);
  EXPECT_EQ(distance_system.stats().rejected_distance, 3U);
}

TEST(ParticleSystemTests, LoadsBasePackStaticAndAnimatedParticles) {
  auto packs = arv::asset::PackManager::load(test_arvane_home());
  ASSERT_TRUE(packs) << packs.error().describe();
  arv::asset::ResourceManager resources(packs.value());
  auto assets = arv::client::load_particle_assets_from_packs(
      packs.value(),
      resources,
      arv::client::default_particle_atlas_options());
  ASSERT_TRUE(assets) << assets.error().describe();

  const arv::client::ParticleType* block_dust =
      assets.value().registry.find("arvane:block_dust");
  ASSERT_NE(block_dust, nullptr);
  ASSERT_EQ(block_dust->frames.size(), 1U);
  EXPECT_EQ(block_dust->frames.front().asset_id, "arvane:texture/particle/critical_hit");

  const arv::client::ParticleType* explosion =
      assets.value().registry.find("arvane:explosion");
  ASSERT_NE(explosion, nullptr);
  ASSERT_EQ(explosion->frames.size(), 16U);
  for (arv::usize index = 0; index < explosion->frames.size(); ++index) {
    EXPECT_EQ(
        explosion->frames[index].asset_id,
        "arvane:texture/particle/explosion_" + std::to_string(index));
  }
  const arv::client::ParticleType* vehicle_smoke =
      assets.value().registry.find("arvane:vehicle_smoke");
  ASSERT_NE(vehicle_smoke, nullptr);
  ASSERT_EQ(vehicle_smoke->frames.size(), 12U);
  for (arv::usize index = 0; index < vehicle_smoke->frames.size(); ++index) {
    EXPECT_EQ(
        vehicle_smoke->frames[index].asset_id,
        "arvane:texture/particle/smoke_" + std::to_string(index));
  }
  EXPECT_FALSE(assets.value().atlas.pages.empty());
}

TEST(ParticleSystemTests, LoadsExtraBlockParticleTexturesIntoParticleAtlas) {
  auto packs = arv::asset::PackManager::load(test_arvane_home());
  ASSERT_TRUE(packs) << packs.error().describe();
  arv::asset::ResourceManager resources(packs.value());
  const std::vector<std::string> extra_textures{
      "arvane:texture/block/dirt",
      "arvane:texture/block/grass_side"};
  auto assets = arv::client::load_particle_assets_from_packs(
      packs.value(),
      resources,
      arv::client::default_particle_atlas_options(),
      extra_textures);
  ASSERT_TRUE(assets) << assets.error().describe();
  EXPECT_NE(assets.value().registry.find_frame("arvane:texture/block/dirt"), nullptr);
  EXPECT_NE(assets.value().registry.find_frame("arvane:texture/block/grass_side"), nullptr);
}
