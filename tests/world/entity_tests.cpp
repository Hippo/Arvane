#include "arv/world/world_runtime.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <utility>

namespace {

arv::EntityID create_goal_driven_entity(
    arv::world::WorldRuntime& world,
    arv::Vec3f position) {
  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:test_goal_jumper";
  info.position = position;

  arv::entity::EntityComponentSet components;
  components.collision = arv::entity::AuthoredCollisionComponent{{0.9F, 0.9F, 0.9F}};
  components.movement = arv::entity::AuthoredMovementComponent{4.5F};
  components.brain = arv::entity::AuthoredBrainComponent{};
  components.brain->tick_interval = 1U;
  components.brain->goals.push_back({
      .type = "arvane:wander",
      .priority = 1,
      .duration_ticks = 20U,
      .animation = "walking"});
  info.authored_components = components;

  const arv::EntityID entity = world.create_entity(std::move(info));
  arv::entity::BrainComponent* brain = world.find_entity_brain(entity);
  EXPECT_NE(brain, nullptr);
  if (brain != nullptr && !brain->goals.empty()) {
    brain->active_goal = 0U;
    brain->goals[0].state.remaining_ticks = 20U;
    brain->goals[0].state.movement_direction = {1.0F, 0.0F, 0.0F};
  }
  return entity;
}

} // namespace

TEST(WorldEntityTests, EntityLifecycleQueuesDespawnAndFlushes) {
  arv::world::WorldRuntime world{arv::WorldID{1}};

  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:test_entity";
  info.position = {1.0F, 2.0F, 3.0F};
  const arv::EntityID entity = world.create_entity(std::move(info));

  EXPECT_TRUE(world.entity_exists(entity));
  EXPECT_EQ(world.entity_count(), 1U);
  ASSERT_TRUE(world.destroy_entity(entity));
  EXPECT_TRUE(world.entity_exists(entity));

  world.flush_entity_despawns();
  EXPECT_FALSE(world.entity_exists(entity));
  EXPECT_EQ(world.entity_count(), 0U);
}

TEST(WorldEntityTests, TransformUpdatesPreviousStateAndChunkRelevance) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:test_entity";
  info.position = {1.0F, 2.0F, 3.0F};
  const arv::EntityID entity = world.create_entity(std::move(info));

  ASSERT_TRUE(world.set_entity_position(entity, {17.0F, 2.0F, -1.0F}));
  const arv::entity::EntityTransform* transform = world.find_entity_transform(entity);
  ASSERT_NE(transform, nullptr);
  EXPECT_EQ(transform->previous_position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));
  EXPECT_EQ(transform->position, (arv::Vec3f{17.0F, 2.0F, -1.0F}));

  ASSERT_TRUE(world.entity_chunk(entity).has_value());
  EXPECT_EQ(world.entity_chunk(entity).value(), (arv::ChunkSectionCoord{{1, 0, -1}}));
}

TEST(WorldEntityTests, AuthoredComponentsCreateBrainMovementAndAnimationState) {
  arv::entity::EntityDefinition definition;
  definition.name = "arvane:test_wanderer";
  definition.components.movement = arv::entity::AuthoredMovementComponent{1.2F};
  definition.components.health = arv::entity::AuthoredHealthComponent{10.0F};
  definition.components.brain = arv::entity::AuthoredBrainComponent{};
  definition.components.brain->tick_interval = 1U;
  definition.components.brain->goals.push_back({
      .type = "arvane:wander",
      .priority = 8,
      .speed_multiplier = 0.5F,
      .duration_ticks = 2U,
      .cooldown_ticks = 2U,
      .animation = "walking"});
  definition.components.brain->goals.push_back({
      .type = "arvane:idle",
      .priority = 10,
      .duration_ticks = 4U,
      .animation = ""});
  definition.components.component_ids = {
      std::string{arv::entity::MovementComponentID},
      std::string{arv::entity::HealthComponentID},
      std::string{arv::entity::BrainComponentID}};

  auto registry_result =
      arv::entity::EntityRegistry::build_with_builtins({std::move(definition)});
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  const std::optional<arv::EntityTypeID> type_id =
      registry_result.value().find_type("arvane:test_wanderer");
  ASSERT_TRUE(type_id.has_value());
  const arv::entity::EntityTypeRecord* type = registry_result.value().type(type_id.value());
  ASSERT_NE(type, nullptr);

  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::world::EntityCreateInfo info;
  info.type_id = type->id;
  info.type_name = type->name;
  info.position = {1.0F, 2.0F, 3.0F};
  info.authored_components = type->components;
  const arv::EntityID entity = world.create_entity(std::move(info));

  ASSERT_NE(world.find_entity_movement(entity), nullptr);
  ASSERT_NE(world.find_entity_health(entity), nullptr);
  ASSERT_NE(world.find_entity_brain(entity), nullptr);
  ASSERT_NE(world.find_entity_animation_state(entity), nullptr);

  world.set_tick_index(1);
  (void)world.tick_entities();
  const arv::entity::EntityAnimationState* animation =
      world.find_entity_animation_state(entity);
  ASSERT_NE(animation, nullptr);
  EXPECT_EQ(animation->name, "walking");
  const arv::entity::EntityTransform* transform = world.find_entity_transform(entity);
  ASSERT_NE(transform, nullptr);
  EXPECT_NE(transform->position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));

  world.set_tick_index(2);
  (void)world.tick_entities();
  world.set_tick_index(3);
  (void)world.tick_entities();
  animation = world.find_entity_animation_state(entity);
  ASSERT_NE(animation, nullptr);
  EXPECT_TRUE(animation->name.empty());
}

TEST(WorldEntityTests, PhysicsEntitiesFallOntoTerrainAndStop) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));

  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:test_physics_entity";
  info.position = {0.5F, 3.0F, 0.5F};
  arv::entity::EntityComponentSet components;
  components.collision = arv::entity::AuthoredCollisionComponent{{0.9F, 0.9F, 0.9F}};
  info.authored_components = components;
  const arv::EntityID entity = world.create_entity(std::move(info));

  for (int tick = 0; tick < 120; ++tick) {
    (void)world.tick_entities(&registry);
  }

  const arv::entity::EntityTransform* transform = world.find_entity_transform(entity);
  const arv::entity::EntityVelocity* velocity = world.find_entity_velocity(entity);
  ASSERT_NE(transform, nullptr);
  ASSERT_NE(velocity, nullptr);
  EXPECT_NEAR(transform->position.y, 1.001F, 0.002F);
  EXPECT_FLOAT_EQ(velocity->linear.y, 0.0F);
}

TEST(WorldEntityTests, DroppedItemsFallOntoTerrainAndStop) {
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));

  const arv::EntityID drop = world.create_dropped_item(
      {stone_item, 1U},
      {0.5F, 3.0F, 0.5F},
      {});

  for (int tick = 0; tick < 120; ++tick) {
    (void)world.tick_dropped_items(
        arv::tick_duration_seconds(arv::DefaultBaseTps),
        &block_registry,
        &item_registry);
  }

  const arv::entity::EntityTransform* transform = world.find_entity_transform(drop);
  const arv::entity::EntityVelocity* velocity = world.find_entity_velocity(drop);
  ASSERT_NE(transform, nullptr);
  ASSERT_NE(velocity, nullptr);
  EXPECT_NEAR(transform->position.y, 1.001F, 0.002F);
  EXPECT_FLOAT_EQ(velocity->linear.y, 0.0F);
}

TEST(WorldEntityTests, DroppedItemsFloatTowardLiquidSurfaceByDefault) {
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();
  const arv::voxel::FluidDefinition water =
      arv::voxel::make_water_fluid_definition();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.place_fluid_source({{0, 1, 0}}, water));

  const arv::EntityID drop = world.create_dropped_item(
      {stone_item, 1U},
      {0.5F, 1.1F, 0.5F},
      {});

  for (int tick = 0; tick < 40; ++tick) {
    (void)world.tick_dropped_items(
        arv::tick_duration_seconds(arv::DefaultBaseTps),
        &block_registry,
        &item_registry);
  }

  const arv::entity::EntityTransform* transform = world.find_entity_transform(drop);
  const arv::entity::EntityVelocity* velocity = world.find_entity_velocity(drop);
  ASSERT_NE(transform, nullptr);
  ASSERT_NE(velocity, nullptr);
  EXPECT_GT(transform->position.y, 1.5F);
  EXPECT_LE(transform->position.y, 1.93F);
  EXPECT_GE(velocity->linear.y, 0.0F);
}

TEST(WorldEntityTests, PhysicsEntitiesCollideWithWallsAndFaceMovementDirection) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 1, 0}}, stone));

  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:test_wall_entity";
  info.position = {0.5F, 1.0F, 0.5F};
  info.velocity = {10.0F, 0.0F, 0.0F};
  arv::entity::EntityComponentSet components;
  components.collision = arv::entity::AuthoredCollisionComponent{{0.9F, 0.9F, 0.9F}};
  info.authored_components = components;
  const arv::EntityID entity = world.create_entity(std::move(info));

  (void)world.tick_entities(&registry);

  const arv::entity::EntityTransform* transform = world.find_entity_transform(entity);
  const arv::entity::EntityVelocity* velocity = world.find_entity_velocity(entity);
  ASSERT_NE(transform, nullptr);
  ASSERT_NE(velocity, nullptr);
  EXPECT_LT(transform->position.x, 0.56F);
  EXPECT_FLOAT_EQ(velocity->linear.x, 0.0F);
  EXPECT_NEAR(transform->rotation.y, std::sin(arv::PiRadians * 0.75F), 0.001F);
  EXPECT_NEAR(transform->rotation.w, std::cos(arv::PiRadians * 0.75F), 0.001F);
}

TEST(WorldEntityTests, GoalDrivenEntitiesJumpWhenBlockedByOneBlockLedge) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 1, 0}}, stone));

  const arv::EntityID entity = create_goal_driven_entity(world, {0.5F, 1.001F, 0.5F});

  (void)world.tick_entities(&registry);

  const arv::entity::EntityVelocity* velocity = world.find_entity_velocity(entity);
  ASSERT_NE(velocity, nullptr);
  EXPECT_GT(velocity->linear.y, 0.0F);
}

TEST(WorldEntityTests, GoalDrivenEntitiesDoNotJumpIntoTallWalls) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 1, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 2, 0}}, stone));

  const arv::EntityID entity = create_goal_driven_entity(world, {0.5F, 1.001F, 0.5F});

  (void)world.tick_entities(&registry);

  const arv::entity::EntityVelocity* velocity = world.find_entity_velocity(entity);
  ASSERT_NE(velocity, nullptr);
  EXPECT_FLOAT_EQ(velocity->linear.y, 0.0F);

  const arv::entity::BrainComponent* brain = world.find_entity_brain(entity);
  ASSERT_NE(brain, nullptr);
  ASSERT_TRUE(brain->active_goal.has_value());
  ASSERT_LT(brain->active_goal.value(), brain->goals.size());
  const arv::Vec3f direction =
      brain->goals[brain->active_goal.value()].state.movement_direction;
  EXPECT_GT(direction.x * direction.x + direction.z * direction.z, 0.0F);
  EXPECT_NE(direction, (arv::Vec3f{1.0F, 0.0F, 0.0F}));

  const arv::entity::MovementIntent* intent = world.find_entity_movement_intent(entity);
  ASSERT_NE(intent, nullptr);
  EXPECT_EQ(intent->desired_direction, direction);
}

TEST(WorldEntityTests, GoalDrivenEntitiesPickOpenDirectionWhenOppositeWallIsBlocked) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  (void)world.ensure_chunk({{-1, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 1, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 2, 0}}, stone));
  ASSERT_TRUE(world.set_block({{-1, 1, 0}}, stone));
  ASSERT_TRUE(world.set_block({{-1, 2, 0}}, stone));

  const arv::EntityID entity = create_goal_driven_entity(world, {0.5F, 1.001F, 0.5F});

  (void)world.tick_entities(&registry);

  const arv::entity::BrainComponent* brain = world.find_entity_brain(entity);
  ASSERT_NE(brain, nullptr);
  ASSERT_TRUE(brain->active_goal.has_value());
  ASSERT_LT(brain->active_goal.value(), brain->goals.size());
  EXPECT_NEAR(
      std::abs(brain->goals[brain->active_goal.value()].state.movement_direction.z),
      1.0F,
      0.15F);

  const arv::entity::EntityTransform* before = world.find_entity_transform(entity);
  ASSERT_NE(before, nullptr);
  const arv::f32 z_before = before->position.z;

  (void)world.tick_entities(&registry);

  const arv::entity::EntityTransform* after = world.find_entity_transform(entity);
  ASSERT_NE(after, nullptr);
  EXPECT_NE(after->position.z, z_before);
}

TEST(WorldEntityTests, PlayerProfileMapsToWorldLocalEntity) {
  arv::world::WorldRuntime world{arv::WorldID{1}};

  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{7});
  EXPECT_EQ(player.player_id, (arv::PlayerID{7}));
  EXPECT_EQ(player.world_id, (arv::WorldID{1}));

  const std::optional<arv::EntityID> entity = world.entity_for_player(arv::PlayerID{7});
  ASSERT_TRUE(entity.has_value());
  EXPECT_EQ(world.player_for_entity(entity.value()), std::optional<arv::PlayerID>{arv::PlayerID{7}});
  EXPECT_NE(world.network_id_for_entity(entity.value()), std::nullopt);
  EXPECT_EQ(world.player_count(), 1U);
  EXPECT_EQ(world.entity_count(), 1U);

  arv::world::WorldRuntime other_world{arv::WorldID{2}};
  arv::player::PlayerState& other_player = other_world.ensure_player(arv::PlayerID{7});
  EXPECT_EQ(other_player.world_id, (arv::WorldID{2}));
  ASSERT_TRUE(other_world.entity_for_player(arv::PlayerID{7}).has_value());
  EXPECT_EQ(other_world.player_count(), 1U);
}

TEST(WorldEntityTests, PlayerVisualPoseClampsHeadYawAndReplicatesHeadRotation) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_player(arv::PlayerID{7});

  ASSERT_TRUE(world.set_player_visual_pose(arv::PlayerID{7}, 1.0F, -0.25F, 0.5F));

  const std::optional<arv::EntityID> entity = world.entity_for_player(arv::PlayerID{7});
  ASSERT_TRUE(entity.has_value());
  const arv::entity::EntityTransform* transform = world.find_entity_transform(entity.value());
  ASSERT_NE(transform, nullptr);
  const float model_yaw_half = (0.5F + arv::ModelForwardYawOffsetRadians) * 0.5F;
  EXPECT_NEAR(transform->rotation.y, std::sin(model_yaw_half), 0.0001F);
  EXPECT_NEAR(transform->rotation.w, std::cos(model_yaw_half), 0.0001F);

  const std::vector<arv::entity::EntitySnapshotEntry> snapshots =
      world.collect_entity_snapshots();
  ASSERT_EQ(snapshots.size(), 1U);
  EXPECT_NEAR(snapshots.front().head_rotation_degrees.x, -14.3239F, 0.001F);
  EXPECT_NEAR(snapshots.front().head_rotation_degrees.y, 28.6479F, 0.001F);
}

TEST(WorldEntityTests, PlayerAnimationStateSelectsLocomotionAndSneakTransitions) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{7});
  const std::optional<arv::EntityID> entity = world.entity_for_player(arv::PlayerID{7});
  ASSERT_TRUE(entity.has_value());

  player.velocity = {1.0F, 0.0F, 0.0F};
  (void)world.tick_entities();
  arv::entity::EntityAnimationState* animation =
      world.find_entity_animation_state(entity.value());
  ASSERT_NE(animation, nullptr);
  EXPECT_EQ(animation->name, "walk");

  player.velocity = {};
  player.swimming = true;
  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "swim_idle");

  player.velocity = {1.0F, 0.0F, 0.0F};
  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "swim");

  player.velocity = {};
  player.swimming = false;
  player.sneaking = true;
  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "sneak");

  animation->time_seconds = 0.25F;
  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "sneak_idle");

  player.velocity = {0.0F, 0.0F, 1.0F};
  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "sneak_walk");

  animation->time_seconds = 1.0F;
  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "sneak_walk");
}

TEST(WorldEntityTests, PlayerHitRightAnimationPlaysOnceBeforeLocomotionResumes) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{7});
  const std::optional<arv::EntityID> entity = world.entity_for_player(arv::PlayerID{7});
  ASSERT_TRUE(entity.has_value());

  player.velocity = {1.0F, 0.0F, 0.0F};
  ASSERT_TRUE(world.set_entity_animation(entity.value(), "hit_right"));
  arv::entity::EntityAnimationState* animation =
      world.find_entity_animation_state(entity.value());
  ASSERT_NE(animation, nullptr);
  animation->time_seconds = 0.10F;

  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "hit_right");

  animation->time_seconds = 0.25F;
  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "walk");
}

TEST(WorldEntityTests, PlayerSneakHitAnimationsPlayOnceBeforeSneakLocomotionResumes) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{7});
  const std::optional<arv::EntityID> entity = world.entity_for_player(arv::PlayerID{7});
  ASSERT_TRUE(entity.has_value());

  player.sneaking = true;
  ASSERT_TRUE(world.set_entity_animation(entity.value(), "sneak_hit_left"));
  arv::entity::EntityAnimationState* animation =
      world.find_entity_animation_state(entity.value());
  ASSERT_NE(animation, nullptr);
  animation->time_seconds = 0.10F;

  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "sneak_hit_left");

  animation->time_seconds = 0.25F;
  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "sneak_idle");

  player.velocity = {0.0F, 0.0F, 1.0F};
  ASSERT_TRUE(world.set_entity_animation(entity.value(), "sneak_hit_right"));
  animation->time_seconds = 0.25F;
  (void)world.tick_entities();
  EXPECT_EQ(animation->name, "sneak_walk");
}

TEST(WorldEntityTests, PersistentEntityRecordsRoundTripThroughWorldRuntime) {
  arv::world::WorldRuntime world{arv::WorldID{3}};
  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:test_persistent";
  info.persistent = true;
  info.persistent_uuid = "entity-test-uuid";
  info.position = {4.0F, 5.0F, 6.0F};
  info.velocity = {0.5F, 0.0F, -0.5F};
  (void)world.create_entity(std::move(info));

  const std::vector<arv::entity::PersistentEntityRecord> records =
      world.persistent_entity_records();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records.front().uuid, "entity-test-uuid");
  EXPECT_EQ(records.front().type_name, "arvane:test_persistent");

  arv::world::WorldRuntime loaded{arv::WorldID{3}};
  loaded.restore_persistent_entities(records);
  EXPECT_EQ(loaded.entity_count(), 1U);
  const std::vector<arv::entity::PersistentEntityRecord> loaded_records =
      loaded.persistent_entity_records();
  ASSERT_EQ(loaded_records.size(), 1U);
  EXPECT_EQ(loaded_records.front().uuid, "entity-test-uuid");
  EXPECT_EQ(loaded_records.front().position, (arv::Vec3f{4.0F, 5.0F, 6.0F}));
}

TEST(WorldEntityTests, DroppedItemsArePersistentEntitiesWithStackPayloads) {
  arv::world::WorldRuntime world{arv::WorldID{3}};
  const arv::EntityID drop = world.create_dropped_item(
      {arv::player::ItemID{4}, 5},
      {1.0F, 2.0F, 3.0F},
      {0.0F, 0.1F, 0.0F},
      0.25F);

  const std::vector<arv::entity::EntitySnapshotEntry> entity_snapshots =
      world.collect_entity_snapshots();
  EXPECT_TRUE(entity_snapshots.empty());

  const std::vector<arv::entity::DroppedItemEntitySnapshot> drop_snapshots =
      world.collect_dropped_item_snapshots();
  ASSERT_EQ(drop_snapshots.size(), 1U);
  EXPECT_EQ(drop_snapshots.front().entity_id, drop);
  EXPECT_EQ(drop_snapshots.front().stack, (arv::player::ItemStack{arv::player::ItemID{4}, 5}));

  const std::vector<arv::entity::PersistentEntityRecord> records =
      world.persistent_entity_records();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records.front().type_name, arv::entity::DroppedItemEntityTypeName);
  ASSERT_TRUE(records.front().dropped_item_stack.has_value());
  EXPECT_EQ(records.front().dropped_item_stack.value(),
            (arv::player::ItemStack{arv::player::ItemID{4}, 5}));

  arv::world::WorldRuntime loaded{arv::WorldID{3}};
  loaded.restore_persistent_entities(records);
  const std::vector<arv::entity::DroppedItemEntitySnapshot> loaded_drops =
      loaded.collect_dropped_item_snapshots();
  ASSERT_EQ(loaded_drops.size(), 1U);
  EXPECT_EQ(loaded_drops.front().stack, (arv::player::ItemStack{arv::player::ItemID{4}, 5}));
  EXPECT_FLOAT_EQ(loaded_drops.front().pickup_delay_seconds, 0.25F);
}
