#include "arv/world/explosion.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] arv::voxel::BlockRegistry make_explosion_block_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());

  arv::voxel::BlockDefinition fragile =
      arv::voxel::make_full_cube_definition("arvane:fragile");
  fragile.blast.resistance = 0.05F;
  fragile.blast.drops_items = false;
  definitions.push_back(std::move(fragile));

  arv::voxel::BlockDefinition shield =
      arv::voxel::make_full_cube_definition("arvane:blast_shield");
  shield.blast.resistance = 0.05F;
  shield.blast.destroyable_by_explosions = false;
  shield.blast.absorbs_blast = true;
  shield.blast.drops_items = false;
  definitions.push_back(std::move(shield));

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  EXPECT_TRUE(result) << result.error().message;
  return std::move(result.value());
}

[[nodiscard]] std::vector<arv::BlockPos> destroyed_positions(
    const arv::world::ExplosionResult& result) {
  std::vector<arv::BlockPos> positions;
  positions.reserve(result.destroyed_blocks.size());
  for (const arv::world::ExplosionBlockHit& hit : result.destroyed_blocks) {
    positions.push_back(hit.pos);
  }
  return positions;
}

[[nodiscard]] bool contains_section(
    const std::vector<arv::ChunkSectionCoord>& sections,
    arv::ChunkSectionCoord coord) {
  return std::find(sections.begin(), sections.end(), coord) != sections.end();
}

arv::EntityID create_damageable_entity(
    arv::world::WorldRuntime& world,
    arv::Vec3f position,
    bool with_velocity = false) {
  arv::entity::EntityComponentSet components;
  components.health = arv::entity::AuthoredHealthComponent{20.0F, 20.0F, false};
  components.collision = arv::entity::AuthoredCollisionComponent{{0.6F, 1.8F, 0.6F}};
  components.velocity = with_velocity;

  arv::world::EntityCreateInfo info;
  info.position = position;
  info.authored_components = components;
  return world.create_entity(std::move(info));
}

} // namespace

TEST(ExplosionTests, SimulationIsDeterministicForSeed) {
  const arv::voxel::BlockRegistry registry = make_explosion_block_registry();
  const std::optional<arv::voxel::BlockVariantID> fragile =
      registry.find_variant("arvane:fragile");
  ASSERT_TRUE(fragile.has_value());

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk_section({{0, 0, 0}});
  for (arv::i32 x = 2; x <= 6; ++x) {
    for (arv::i32 y = 2; y <= 6; ++y) {
      for (arv::i32 z = 2; z <= 6; ++z) {
        ASSERT_TRUE(world.set_block({{x, y, z}}, fragile.value()));
      }
    }
  }

  arv::world::ExplosionRequest request;
  request.world_id = world.id();
  request.origin = {4.5F, 4.5F, 4.5F};
  request.profile_id = "arvane:debug";
  request.seed = 1234U;
  request.overrides.radius = 3.0F;
  request.overrides.power = 9.0F;
  request.overrides.max_destroyed_blocks = 64U;
  request.overrides.max_rays = 128U;
  request.overrides.drops_enabled = false;
  request.overrides.entity_damage_enabled = false;

  auto first = arv::world::simulate_explosion(world, registry, request);
  auto second = arv::world::simulate_explosion(world, registry, request);

  ASSERT_TRUE(first) << first.error().message;
  ASSERT_TRUE(second) << second.error().message;
  EXPECT_EQ(destroyed_positions(first.value()), destroyed_positions(second.value()));
  EXPECT_FALSE(first.value().destroyed_blocks.empty());
  EXPECT_LE(first.value().destroyed_blocks.size(), 64U);
}

TEST(ExplosionTests, BlockDamageOverrideLeavesBlocksUntouched) {
  const arv::voxel::BlockRegistry registry = make_explosion_block_registry();
  const std::optional<arv::voxel::BlockVariantID> fragile =
      registry.find_variant("arvane:fragile");
  ASSERT_TRUE(fragile.has_value());

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk_section({{0, 0, 0}});
  const arv::BlockPos target{{4, 4, 4}};
  ASSERT_TRUE(world.set_block(target, fragile.value()));

  arv::world::ExplosionRequest request;
  request.world_id = world.id();
  request.origin = {4.5F, 4.5F, 4.5F};
  request.profile_id = "arvane:small";
  request.seed = 42U;
  request.overrides.block_damage_enabled = false;
  request.overrides.entity_damage_enabled = false;

  auto result = arv::world::execute_explosion(world, registry, request);

  ASSERT_TRUE(result) << result.error().message;
  EXPECT_TRUE(result.value().destroyed_blocks.empty());
  EXPECT_EQ(world.get_block(target), fragile.value());
}

TEST(ExplosionTests, UndestroyableBlastBlocksAbsorbExplosion) {
  const arv::voxel::BlockRegistry registry = make_explosion_block_registry();
  const std::optional<arv::voxel::BlockVariantID> shield =
      registry.find_variant("arvane:blast_shield");
  ASSERT_TRUE(shield.has_value());

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk_section({{0, 0, 0}});
  const arv::BlockPos target{{4, 4, 4}};
  ASSERT_TRUE(world.set_block(target, shield.value()));

  arv::world::ExplosionRequest request;
  request.world_id = world.id();
  request.origin = {4.5F, 4.5F, 4.5F};
  request.profile_id = "arvane:debug";
  request.seed = 77U;
  request.overrides.power = 40.0F;
  request.overrides.radius = 2.0F;
  request.overrides.entity_damage_enabled = false;

  auto result = arv::world::execute_explosion(world, registry, request);

  ASSERT_TRUE(result) << result.error().message;
  EXPECT_TRUE(result.value().destroyed_blocks.empty());
  EXPECT_EQ(world.get_block(target), shield.value());
}

TEST(ExplosionTests, EntityDamageOverridePreservesHealth) {
  const arv::voxel::BlockRegistry registry = make_explosion_block_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk_section({{0, 0, 0}});
  const arv::EntityID target =
      create_damageable_entity(world, {5.5F, 4.0F, 4.5F}, true);

  arv::world::ExplosionRequest request;
  request.world_id = world.id();
  request.origin = {4.5F, 4.5F, 4.5F};
  request.profile_id = "arvane:debug";
  request.seed = 91U;
  request.overrides.block_damage_enabled = false;
  request.overrides.entity_damage_enabled = false;

  auto result = arv::world::execute_explosion(world, registry, request);

  ASSERT_TRUE(result) << result.error().message;
  EXPECT_TRUE(result.value().entity_hits.empty());
  const arv::entity::EntityHealth* health = world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 20.0F);
  const arv::entity::EntityVelocity* velocity = world.find_entity_velocity(target);
  ASSERT_NE(velocity, nullptr);
  EXPECT_EQ(velocity->linear, (arv::Vec3f{}));
}

TEST(ExplosionTests, ExplosionDamagesAndPushesNearbyEntities) {
  const arv::voxel::BlockRegistry registry = make_explosion_block_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk_section({{0, 0, 0}});
  const arv::EntityID target =
      create_damageable_entity(world, {5.5F, 4.0F, 4.5F}, true);

  arv::world::ExplosionRequest request;
  request.world_id = world.id();
  request.origin = {4.5F, 4.5F, 4.5F};
  request.profile_id = "arvane:debug";
  request.seed = 92U;
  request.overrides.block_damage_enabled = false;

  auto result = arv::world::execute_explosion(world, registry, request);

  ASSERT_TRUE(result) << result.error().message;
  ASSERT_EQ(result.value().entity_hits.size(), 1U);
  EXPECT_EQ(result.value().entity_hits.front().entity, target);
  EXPECT_TRUE(result.value().entity_hits.front().damage_applied);
  const arv::entity::EntityHealth* health = world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_LT(health->current, 20.0F);
  const arv::entity::EntityVelocity* velocity = world.find_entity_velocity(target);
  ASSERT_NE(velocity, nullptr);
  EXPECT_GT(arv::length(velocity->linear), 0.0F);
}

TEST(ExplosionTests, SolidBlocksOccludeEntityExposure) {
  const arv::voxel::BlockRegistry registry = make_explosion_block_registry();
  const std::optional<arv::voxel::BlockVariantID> shield =
      registry.find_variant("arvane:blast_shield");
  ASSERT_TRUE(shield.has_value());

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk_section({{0, 0, 0}});
  for (arv::i32 y = 3; y <= 6; ++y) {
    for (arv::i32 z = 3; z <= 5; ++z) {
      ASSERT_TRUE(world.set_block({{5, y, z}}, shield.value()));
    }
  }
  const arv::EntityID target =
      create_damageable_entity(world, {7.0F, 4.0F, 4.5F}, true);

  arv::world::ExplosionRequest request;
  request.world_id = world.id();
  request.origin = {3.5F, 4.5F, 4.5F};
  request.profile_id = "arvane:debug";
  request.seed = 93U;
  request.overrides.radius = 6.0F;
  request.overrides.block_damage_enabled = false;

  auto result = arv::world::execute_explosion(world, registry, request);

  ASSERT_TRUE(result) << result.error().message;
  EXPECT_TRUE(result.value().entity_hits.empty());
  const arv::entity::EntityHealth* health = world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 20.0F);
}

TEST(ExplosionTests, BoundaryBlockMutationReportsNeighborDirtySection) {
  const arv::voxel::BlockRegistry registry = make_explosion_block_registry();
  const std::optional<arv::voxel::BlockVariantID> fragile =
      registry.find_variant("arvane:fragile");
  ASSERT_TRUE(fragile.has_value());

  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk_section({{0, 0, 0}});
  (void)world.ensure_chunk_section({{1, 0, 0}});
  const arv::BlockPos target{{15, 4, 4}};
  ASSERT_TRUE(world.set_block(target, fragile.value()));

  arv::world::ExplosionRequest request;
  request.world_id = world.id();
  request.origin = {15.5F, 4.5F, 4.5F};
  request.profile_id = "arvane:debug";
  request.seed = 94U;
  request.overrides.radius = 1.2F;
  request.overrides.power = 12.0F;
  request.overrides.max_destroyed_blocks = 1U;
  request.overrides.max_rays = 64U;
  request.overrides.entity_damage_enabled = false;

  auto result = arv::world::execute_explosion(world, registry, request);

  ASSERT_TRUE(result) << result.error().message;
  ASSERT_EQ(result.value().destroyed_blocks.size(), 1U);
  EXPECT_EQ(result.value().destroyed_blocks.front().pos, target);
  EXPECT_TRUE(contains_section(result.value().dirty_sections, {{0, 0, 0}}));
  EXPECT_TRUE(contains_section(result.value().dirty_sections, {{1, 0, 0}}));
}
