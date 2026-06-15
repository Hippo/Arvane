#include "arv/base/base_module.hpp"
#include "arv/world/entity_goal_registry.hpp"
#include "arv/world/world_runtime.hpp"
#include "support/entity_animation.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <utility>

namespace {

using arv::testsupport::find_active_animation;

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
  components.brain->goals.push_back({.type = "arvane:wander",
                                     .priority = 1,
                                     .duration_ticks = 20U,
                                     .animation = "walking"});
  info.authored_components = components;

  const arv::EntityID entity = world.create_entity(std::move(info));
  arv::entity::BrainComponent* brain = world.find_entity_brain(entity);
  EXPECT_NE(brain, nullptr);
  if (brain != nullptr && !brain->goals.empty()) {
    brain->goals[0].state.active = true;
    brain->goals[0].state.remaining_ticks = 20U;
    brain->goals[0].state.movement_direction = {1.0F, 0.0F, 0.0F};
  }
  return entity;
}

arv::world::EntityGoalRegistry make_base_goal_registry() {
  arv::world::EntityGoalRegistry registry;
  arv::base::register_base_entity_goals(registry);
  return registry;
}

arv::entity::VehicleDefinitionRegistry make_test_vehicle_registry() {
  arv::entity::VehicleDefinition buggy;
  buggy.name = "arvane:test_buggy";
  buggy.kind = arv::entity::VehicleKind::Vehicle;
  buggy.physics = arv::entity::VehiclePhysicsKind::GroundVehicle;
  buggy.collision_size = {1.8F, 1.2F, 3.2F};
  buggy.seats.push_back({"driver",
                         "driver",
                         arv::entity::SeatRole::Driver,
                         {0.0F, 0.8F, 0.0F},
                         {},
                         true,
                         arv::entity::CameraMode::ThirdPersonFollow});
  buggy.seats.push_back({"passenger",
                         "passenger",
                         arv::entity::SeatRole::Passenger,
                         {0.5F, 0.8F, 0.0F},
                         {},
                         false,
                         arv::entity::CameraMode::ThirdPersonFollow});
  buggy.ground = {
      12.0F,
      4.0F,
      8.0F,
      16.0F,
      0.9F,
      80.0F,
      0.8F,
      1.0F,
      0.5F};
  buggy.authority.max_speed = 14.0F;
  buggy.authority.max_acceleration = 12.0F;
  buggy.authority.max_deceleration = 20.0F;
  buggy.authority.max_angular_speed = 120.0F;
  buggy.authority.max_vertical_speed = 20.0F;
  buggy.authority.max_step_height = 0.6F;
  buggy.authority.requires_ground = true;
  buggy.authority.requires_water = false;
  auto registry = arv::entity::VehicleDefinitionRegistry::build({std::move(buggy)});
  EXPECT_TRUE(registry);
  return std::move(registry.value());
}

const arv::entity::GoalSlot* first_active_goal(
    const arv::entity::BrainComponent& brain) {
  for (const arv::entity::GoalSlot& goal : brain.goals) {
    if (goal.state.active) {
      return &goal;
    }
  }
  return nullptr;
}

const arv::entity::GoalSlot* find_goal(
    const arv::entity::BrainComponent& brain,
    std::string_view type) {
  for (const arv::entity::GoalSlot& goal : brain.goals) {
    if (goal.type == type) {
      return &goal;
    }
  }
  return nullptr;
}

bool goal_is_active(
    const arv::entity::BrainComponent& brain,
    std::string_view type) {
  const arv::entity::GoalSlot* goal = find_goal(brain, type);
  return goal != nullptr && goal->state.active;
}

arv::entity::VehicleDefinitionRegistry make_test_living_mount_registry() {
  arv::entity::VehicleDefinition mount;
  mount.name = "arvane:test_horse";
  mount.kind = arv::entity::VehicleKind::Mount;
  mount.physics = arv::entity::VehiclePhysicsKind::LivingMount;
  mount.collision_size = {1.4F, 1.6F, 1.4F};
  mount.seats.push_back({"saddle",
                         "saddle",
                         arv::entity::SeatRole::Driver,
                         {0.0F, 1.1F, 0.0F},
                         {},
                         true,
                         arv::entity::CameraMode::MountedThirdPerson});
  mount.living_mount.walk_speed = 4.0F;
  mount.living_mount.sprint_speed = 7.0F;
  mount.living_mount.acceleration = 16.0F;
  mount.living_mount.turn_rate = arv::degrees_to_radians(180.0F);
  mount.living_mount.jump_strength = 7.0F;
  mount.living_mount.gravity_scale = 1.0F;
  mount.living_mount.step_height = 0.6F;
  mount.living_mount.friction = 0.82F;
  mount.authority.max_speed = 8.0F;
  mount.authority.max_acceleration = 16.0F;
  mount.authority.max_deceleration = 22.0F;
  mount.authority.max_angular_speed = arv::degrees_to_radians(180.0F);
  mount.authority.max_vertical_speed = 8.0F;
  mount.authority.max_step_height = 0.6F;
  mount.authority.requires_ground = true;
  auto registry = arv::entity::VehicleDefinitionRegistry::build({std::move(mount)});
  EXPECT_TRUE(registry);
  return std::move(registry.value());
}

arv::entity::EntityComponentSet make_hostile_brain_components(float damage) {
  arv::entity::EntityComponentSet components;
  components.collision = arv::entity::AuthoredCollisionComponent{{0.8F, 1.2F, 0.8F}};
  components.movement = arv::entity::AuthoredMovementComponent{2.0F};
  components.health = arv::entity::AuthoredHealthComponent{10.0F, 10.0F, false};
  components.brain = arv::entity::AuthoredBrainComponent{};
  components.brain->tick_interval = 1U;
  components.brain->goals.push_back({.type = "arvane:nearest_target",
                                     .priority = 1,
                                     .radius = 8.0F,
                                     .duration_ticks = 80U,
                                     .memory_ticks = 40U,
                                     .target = "nearest_player"});
  components.brain->goals.push_back({.type = "arvane:move_to_target",
                                     .priority = 2,
                                     .speed_multiplier = 1.0F,
                                     .duration_ticks = 80U,
                                     .range = 0.5F,
                                     .animation = "walk"});
  components.brain->goals.push_back({.type = "arvane:look_at_target",
                                     .priority = 2,
                                     .duration_ticks = 80U});
  components.brain->goals.push_back({.type = "arvane:melee_attack",
                                     .priority = 3,
                                     .duration_ticks = 80U,
                                     .cooldown_ticks = 10U,
                                     .range = 3.0F,
                                     .damage = damage,
                                     .attack_animation = "bite"});
  return components;
}

arv::EntityID create_test_rideable(
    arv::world::WorldRuntime& world,
    const arv::entity::VehicleDefinitionRegistry& vehicles,
    arv::Vec3f position = {0.0F, 1.0F, 0.0F}) {
  arv::entity::EntityComponentSet components;
  components.collision = arv::entity::AuthoredCollisionComponent{{1.8F, 1.2F, 3.2F}};
  components.rideable = arv::entity::AuthoredRideableComponent{"arvane:test_buggy"};
  components.vehicle_physics =
      arv::entity::AuthoredVehiclePhysicsComponent{"arvane:test_buggy"};

  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:test_buggy";
  info.position = position;
  info.authored_components = components;
  info.vehicle_definitions = &vehicles;
  return world.create_entity(std::move(info));
}

void place_test_floor(
    arv::world::WorldRuntime& world,
    arv::voxel::BlockVariantID block,
    arv::i32 min_x,
    arv::i32 max_x,
    arv::i32 min_z,
    arv::i32 max_z) {
  for (arv::i32 z = min_z; z <= max_z; ++z) {
    for (arv::i32 x = min_x; x <= max_x; ++x) {
      (void)world.ensure_chunk(arv::block_to_chunk({{x, 0, z}}));
      ASSERT_TRUE(world.set_block({{x, 0, z}}, block));
    }
  }
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

TEST(WorldEntityTests, RuntimeTagsAreRemovedWithEntityLifecycle) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:test_entity";
  const arv::EntityID entity = world.create_entity(std::move(info));

  ASSERT_TRUE(world.add_entity_tag(entity, "test:tagged"));
  EXPECT_TRUE(world.entity_has_tag(entity, "test:tagged"));
  ASSERT_TRUE(world.remove_entity_tag(entity, "test:tagged"));
  EXPECT_FALSE(world.entity_has_tag(entity, "test:tagged"));
  ASSERT_TRUE(world.add_entity_tag(entity, "test:tagged"));

  ASSERT_TRUE(world.destroy_entity(entity));
  world.flush_entity_despawns();
  EXPECT_FALSE(world.entity_has_tag(entity, "test:tagged"));
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

TEST(WorldEntityTests, RideableSeatAssignmentEnforcesInvariants) {
  const arv::entity::VehicleDefinitionRegistry vehicles = make_test_vehicle_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  const arv::EntityID rideable = create_test_rideable(world, vehicles);
  const arv::EntityID passenger = world.create_entity();
  const arv::EntityID second_passenger = world.create_entity();
  const arv::EntityID other_rideable = create_test_rideable(world, vehicles, {4.0F, 1.0F, 0.0F});

  ASSERT_TRUE(world.mount_entity(rideable, passenger, std::string_view{"driver"}));
  EXPECT_EQ(world.mounted_rideable(passenger), rideable);
  EXPECT_EQ(world.mounted_seat(passenger), std::optional<std::string>{"driver"});

  const arv::entity::RideableComponent* rideable_state = world.find_entity_rideable(rideable);
  ASSERT_NE(rideable_state, nullptr);
  EXPECT_EQ(rideable_state->controller, passenger);
  ASSERT_EQ(rideable_state->seats.size(), 2U);
  EXPECT_EQ(rideable_state->seats.front().occupant, passenger);
  EXPECT_FALSE(world.mount_entity(rideable, second_passenger, std::string_view{"driver"}));
  EXPECT_FALSE(world.mount_entity(other_rideable, passenger, std::string_view{"driver"}));

  ASSERT_TRUE(world.dismount_entity(passenger));
  EXPECT_FALSE(world.mounted_rideable(passenger).has_value());
  rideable_state = world.find_entity_rideable(rideable);
  ASSERT_NE(rideable_state, nullptr);
  EXPECT_EQ(rideable_state->controller, (arv::EntityID{}));
  EXPECT_EQ(rideable_state->seats.front().occupant, (arv::EntityID{}));
}

TEST(WorldEntityTests, AuthoredVehicleComponentsRequireResolvedDefinition) {
  arv::world::WorldRuntime world{arv::WorldID{1}};

  arv::entity::EntityComponentSet components;
  components.rideable = arv::entity::AuthoredRideableComponent{"arvane:missing"};
  components.vehicle_physics =
      arv::entity::AuthoredVehiclePhysicsComponent{"arvane:missing"};

  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:missing_vehicle";
  info.authored_components = components;

  const arv::EntityID entity = world.create_entity(std::move(info));

  EXPECT_EQ(entity, (arv::EntityID{}));
  EXPECT_EQ(world.entity_count(), 0U);
}

TEST(WorldEntityTests, UnmountedVehiclePhysicsDampsServerOwnedMotion) {
  const arv::entity::VehicleDefinitionRegistry vehicles = make_test_vehicle_registry();
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  place_test_floor(world, stone, -4, 6, -4, 4);
  const arv::EntityID rideable = create_test_rideable(world, vehicles);
  arv::entity::EntityVelocity* velocity = world.find_entity_velocity(rideable);
  ASSERT_NE(velocity, nullptr);
  velocity->linear = {6.0F, 0.0F, 0.0F};
  arv::entity::VehiclePhysicsComponent* vehicle_physics =
      world.find_entity_vehicle_physics(rideable);
  ASSERT_NE(vehicle_physics, nullptr);
  vehicle_physics->angular_velocity = {0.0F, 1.0F, 0.0F};

  for (int tick = 0; tick < 120; ++tick) {
    (void)world.tick_entities(&block_registry, nullptr, &vehicles);
  }

  const arv::entity::EntityTransform* transform = world.find_entity_transform(rideable);
  ASSERT_NE(transform, nullptr);
  EXPECT_GT(transform->position.x, 0.5F);
  velocity = world.find_entity_velocity(rideable);
  ASSERT_NE(velocity, nullptr);
  EXPECT_NEAR(arv::horizontal_length(velocity->linear), 0.0F, 0.0001F);
  const arv::entity::VehiclePhysicsComponent* physics =
      world.find_entity_vehicle_physics(rideable);
  ASSERT_NE(physics, nullptr);
  EXPECT_TRUE(physics->grounded);
  EXPECT_NEAR(arv::horizontal_length(physics->linear_velocity), 0.0F, 0.0001F);
  EXPECT_NEAR(arv::length(physics->angular_velocity), 0.0F, 0.0001F);
}

TEST(WorldEntityTests, MountedVehiclePhysicsDoesNotAdvanceControllerOwnedVehicle) {
  const arv::entity::VehicleDefinitionRegistry vehicles = make_test_vehicle_registry();
  const arv::voxel::BlockRegistry block_registry =
      arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      block_registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  place_test_floor(world, stone, -4, 6, -4, 4);
  const arv::EntityID rideable = create_test_rideable(world, vehicles);
  const arv::EntityID passenger = world.create_entity();
  ASSERT_TRUE(world.mount_entity(rideable, passenger, std::string_view{"driver"}));
  arv::entity::EntityVelocity* velocity = world.find_entity_velocity(rideable);
  ASSERT_NE(velocity, nullptr);
  velocity->linear = {6.0F, 0.0F, 0.0F};

  for (int tick = 0; tick < 20; ++tick) {
    (void)world.tick_entities(&block_registry, nullptr, &vehicles);
  }

  const arv::entity::EntityTransform* transform = world.find_entity_transform(rideable);
  ASSERT_NE(transform, nullptr);
  EXPECT_EQ(transform->position, (arv::Vec3f{0.0F, 1.0F, 0.0F}));
  velocity = world.find_entity_velocity(rideable);
  ASSERT_NE(velocity, nullptr);
  EXPECT_EQ(velocity->linear, (arv::Vec3f{6.0F, 0.0F, 0.0F}));
}

TEST(WorldEntityTests, RideableDespawnDismountsPassengersAndRecordsRideEndedEvent) {
  const arv::entity::VehicleDefinitionRegistry vehicles = make_test_vehicle_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  const arv::EntityID rideable = create_test_rideable(world, vehicles);

  arv::world::EntityCreateInfo passenger_info;
  passenger_info.type_name = "arvane:test_passenger";
  passenger_info.position = {1.0F, 2.0F, 3.0F};
  const arv::EntityID passenger = world.create_entity(std::move(passenger_info));
  const std::optional<arv::EntityNetID> rideable_net =
      world.network_id_for_entity(rideable);
  const std::optional<arv::EntityNetID> passenger_net =
      world.network_id_for_entity(passenger);
  ASSERT_TRUE(rideable_net.has_value());
  ASSERT_TRUE(passenger_net.has_value());

  ASSERT_TRUE(world.mount_entity(rideable, passenger, std::string_view{"driver"}));
  ASSERT_TRUE(world.destroy_entity(rideable));
  world.flush_entity_despawns();

  EXPECT_FALSE(world.entity_exists(rideable));
  EXPECT_TRUE(world.entity_exists(passenger));
  EXPECT_FALSE(world.mounted_rideable(passenger).has_value());
  EXPECT_EQ(world.find_entity_attachment(passenger), nullptr);

  std::vector<arv::world::RideEndedEvent> events = world.drain_ride_ended_events();
  ASSERT_EQ(events.size(), 1U);
  EXPECT_EQ(events.front().passenger, passenger);
  EXPECT_EQ(events.front().rideable, rideable);
  EXPECT_EQ(events.front().passenger_net_id, passenger_net);
  EXPECT_EQ(events.front().rideable_net_id, rideable_net);
}

TEST(WorldEntityTests, PassengerTransformDerivesFromSeatTransform) {
  const arv::entity::VehicleDefinitionRegistry vehicles = make_test_vehicle_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  const arv::EntityID rideable = create_test_rideable(world, vehicles, {10.0F, 2.0F, -3.0F});
  const arv::EntityID passenger = world.create_entity();

  ASSERT_TRUE(world.mount_entity(rideable, passenger, std::string_view{"passenger"}));
  const arv::entity::EntityTransform* transform = world.find_entity_transform(passenger);
  ASSERT_NE(transform, nullptr);
  EXPECT_EQ(transform->position, (arv::Vec3f{10.5F, 2.8F, -3.0F}));

  ASSERT_TRUE(world.set_entity_transform(rideable, {20.0F, 3.0F, 1.0F}, arv::yaw_rotation(arv::PiRadians)));
  ASSERT_TRUE(world.update_passenger_attachment_transform(passenger));
  transform = world.find_entity_transform(passenger);
  ASSERT_NE(transform, nullptr);
  EXPECT_NEAR(transform->position.x, 19.5F, 0.0001F);
  EXPECT_NEAR(transform->position.y, 3.8F, 0.0001F);
  EXPECT_NEAR(transform->position.z, 1.0F, 0.0001F);
}

TEST(WorldEntityTests, SafeDismountFindsValidSidePosition) {
  const arv::entity::VehicleDefinitionRegistry vehicles = make_test_vehicle_registry();
  const arv::voxel::BlockRegistry blocks = arv::testsupport::make_test_block_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  for (arv::i32 z = -4; z <= 4; ++z) {
    for (arv::i32 x = -4; x <= 4; ++x) {
      const arv::BlockPos pos{{x, 0, z}};
      world.ensure_chunk(arv::block_to_chunk(pos));
      ASSERT_TRUE(world.set_block(pos, arv::voxel::BlockVariantID{2}));
    }
  }

  const arv::EntityID rideable = create_test_rideable(world, vehicles, {0.0F, 1.0F, 0.0F});
  const std::optional<arv::Vec3f> dismount =
      world.find_safe_dismount_position(rideable, blocks);

  ASSERT_TRUE(dismount.has_value());
  EXPECT_GE(dismount->y, 1.0F);
  EXPECT_LT(arv::distance(dismount.value(), (arv::Vec3f{0.0F, 1.0F, 0.0F})), 8.0F);
}

TEST(WorldEntityTests, SafeDismountFailsSafelyWhenBlocked) {
  const arv::entity::VehicleDefinitionRegistry vehicles = make_test_vehicle_registry();
  const arv::voxel::BlockRegistry blocks = arv::testsupport::make_test_block_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.ensure_chunk({{0, 0, 0}});
  for (arv::i32 z = -5; z <= 5; ++z) {
    for (arv::i32 y = 0; y <= 4; ++y) {
      for (arv::i32 x = -5; x <= 5; ++x) {
        const arv::BlockPos pos{{x, y, z}};
        world.ensure_chunk(arv::block_to_chunk(pos));
        ASSERT_TRUE(world.set_block(pos, arv::voxel::BlockVariantID{2}));
      }
    }
  }

  const arv::EntityID rideable = create_test_rideable(world, vehicles, {0.0F, 1.0F, 0.0F});
  EXPECT_FALSE(world.find_safe_dismount_position(rideable, blocks).has_value());
}

TEST(WorldEntityTests, AuthoredComponentsCreateBrainMovementAndAnimationState) {
  arv::entity::EntityDefinition definition;
  definition.name = "arvane:test_wanderer";
  definition.components.movement = arv::entity::AuthoredMovementComponent{1.2F};
  definition.components.health = arv::entity::AuthoredHealthComponent{10.0F};
  definition.components.brain = arv::entity::AuthoredBrainComponent{};
  definition.components.brain->tick_interval = 1U;
  definition.components.brain->goals.push_back({.type = "arvane:wander",
                                                .priority = 8,
                                                .speed_multiplier = 0.5F,
                                                .duration_ticks = 2U,
                                                .cooldown_ticks = 2U,
                                                .animation = "walking"});
  definition.components.brain->goals.push_back({.type = "arvane:idle",
                                                .priority = 10,
                                                .duration_ticks = 4U,
                                                .animation = ""});
  definition.components.component_ids = {
      std::string{arv::entity::MovementComponentID},
      std::string{arv::entity::HealthComponentID},
      std::string{arv::entity::BrainComponentID}};

  auto registry_result =
      arv::base::build_base_entity_registry({std::move(definition)});
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
  arv::world::EntityGoalRegistry goals = make_base_goal_registry();

  ASSERT_NE(world.find_entity_movement(entity), nullptr);
  ASSERT_NE(world.find_entity_health(entity), nullptr);
  ASSERT_NE(world.find_entity_brain(entity), nullptr);
  ASSERT_NE(world.find_entity_animation_state(entity), nullptr);

  world.set_tick_index(1);
  (void)world.tick_entities(nullptr, &goals);
  const arv::entity::EntityAnimationState* animation =
      world.find_entity_animation_state(entity);
  ASSERT_NE(animation, nullptr);
  ASSERT_EQ(animation->active.size(), 1U);
  EXPECT_EQ(animation->active.front().name, "walking");
  const arv::entity::EntityTransform* transform = world.find_entity_transform(entity);
  ASSERT_NE(transform, nullptr);
  EXPECT_NE(transform->position, (arv::Vec3f{1.0F, 2.0F, 3.0F}));

  world.set_tick_index(2);
  (void)world.tick_entities(nullptr, &goals);
  world.set_tick_index(3);
  (void)world.tick_entities(nullptr, &goals);
  animation = world.find_entity_animation_state(entity);
  ASSERT_NE(animation, nullptr);
  EXPECT_TRUE(animation->active.empty());
}

TEST(WorldEntityTests, MultiGoalBrainRunsTargetMoveLookAndAttackTogether) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{7});
  player.position = {2.5F, 0.0F, 0.0F};
  arv::world::EntityGoalRegistry goals = make_base_goal_registry();

  arv::world::EntityCreateInfo info;
  info.type_name = "test:hostile_pig";
  info.position = {0.0F, 0.0F, 0.0F};
  info.authored_components = make_hostile_brain_components(2.0F);
  const arv::EntityID hostile = world.create_entity(std::move(info));

  world.set_tick_index(1);
  (void)world.tick_entities(nullptr, &goals);
  world.set_tick_index(2);
  (void)world.tick_entities(nullptr, &goals);

  const arv::entity::BrainComponent* brain = world.find_entity_brain(hostile);
  ASSERT_NE(brain, nullptr);
  EXPECT_TRUE(goal_is_active(*brain, "arvane:nearest_target"));
  EXPECT_TRUE(goal_is_active(*brain, "arvane:move_to_target"));
  EXPECT_TRUE(goal_is_active(*brain, "arvane:look_at_target"));
  EXPECT_TRUE(goal_is_active(*brain, "arvane:melee_attack"));

  const std::optional<arv::EntityID> player_entity =
      world.entity_for_player(arv::PlayerID{7});
  ASSERT_TRUE(player_entity.has_value());
  const arv::entity::EntityHealth* health =
      world.find_entity_health(player_entity.value());
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 18.0F);

  const arv::entity::EntityTransform* transform = world.find_entity_transform(hostile);
  ASSERT_NE(transform, nullptr);
  EXPECT_GT(transform->position.x, 0.0F);

  const arv::entity::EntityAnimationState* animation =
      world.find_entity_animation_state(hostile);
  ASSERT_NE(animation, nullptr);
  EXPECT_NE(find_active_animation(*animation, "walk"), nullptr);
  EXPECT_NE(find_active_animation(*animation, "bite"), nullptr);
}

TEST(WorldEntityTests, MeleeAttackRequiresDamageAndHonorsCooldownAndRange) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{7});
  player.position = {2.0F, 0.0F, 0.0F};
  const std::optional<arv::EntityID> player_entity =
      world.entity_for_player(arv::PlayerID{7});
  ASSERT_TRUE(player_entity.has_value());
  arv::world::EntityGoalRegistry goals = make_base_goal_registry();

  arv::world::EntityCreateInfo harmless_info;
  harmless_info.type_name = "test:harmless";
  harmless_info.position = {0.0F, 0.0F, 0.0F};
  harmless_info.authored_components = make_hostile_brain_components(0.0F);
  const arv::EntityID harmless = world.create_entity(std::move(harmless_info));
  ASSERT_NE(harmless, (arv::EntityID{}));

  world.set_tick_index(1);
  (void)world.tick_entities(nullptr, &goals);
  world.set_tick_index(2);
  (void)world.tick_entities(nullptr, &goals);
  const arv::entity::EntityHealth* health =
      world.find_entity_health(player_entity.value());
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 20.0F);

  arv::world::EntityCreateInfo attacker_info;
  attacker_info.type_name = "test:attacker";
  attacker_info.position = {0.0F, 0.0F, 0.0F};
  attacker_info.authored_components = make_hostile_brain_components(3.0F);
  const arv::EntityID attacker = world.create_entity(std::move(attacker_info));
  ASSERT_NE(attacker, (arv::EntityID{}));

  world.set_tick_index(3);
  (void)world.tick_entities(nullptr, &goals);
  world.set_tick_index(4);
  (void)world.tick_entities(nullptr, &goals);
  EXPECT_FLOAT_EQ(health->current, 17.0F);
  world.set_tick_index(5);
  (void)world.tick_entities(nullptr, &goals);
  EXPECT_FLOAT_EQ(health->current, 17.0F);

  player.position = {20.0F, 0.0F, 0.0F};
  for (int tick = 0; tick < 12; ++tick) {
    world.set_tick_index(static_cast<arv::TickIndex>(6 + tick));
    (void)world.tick_entities(nullptr, &goals);
  }
  EXPECT_FLOAT_EQ(health->current, 17.0F);

  const arv::entity::BrainComponent* brain = world.find_entity_brain(attacker);
  ASSERT_NE(brain, nullptr);
  EXPECT_FALSE(goal_is_active(*brain, "arvane:melee_attack"));
}

TEST(WorldEntityTests, SwimInFluidMovesTowardDrySupportedEscape) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      registry.find_variant("arvane:stone").value();
  const arv::voxel::BlockVariantID water =
      registry.find_variant("arvane:water").value();
  arv::world::EntityGoalRegistry goals = make_base_goal_registry();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{2, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{0, 1, 0}}, water));

  arv::entity::EntityComponentSet components;
  components.collision = arv::entity::AuthoredCollisionComponent{{0.8F, 0.9F, 0.8F}};
  components.movement = arv::entity::AuthoredMovementComponent{2.0F};
  components.brain = arv::entity::AuthoredBrainComponent{};
  components.brain->tick_interval = 1U;
  components.brain->goals.push_back({.type = "arvane:swim_in_fluid",
                                     .priority = 0,
                                     .speed_multiplier = 1.0F,
                                     .duration_ticks = 20U,
                                     .animation = "walk"});
  arv::world::EntityCreateInfo info;
  info.type_name = "test:swimmer";
  info.position = {0.5F, 1.0F, 0.5F};
  info.authored_components = components;
  const arv::EntityID swimmer = world.create_entity(std::move(info));

  world.set_tick_index(1);
  (void)world.tick_entities(&registry, &goals);

  const arv::entity::MovementIntent* intent =
      world.find_entity_movement_intent(swimmer);
  ASSERT_NE(intent, nullptr);
  EXPECT_GT(intent->desired_direction.y, 0.0F);
  EXPECT_GT(intent->desired_direction.x, 0.0F);
  const arv::entity::EntityTransform* transform =
      world.find_entity_transform(swimmer);
  ASSERT_NE(transform, nullptr);
  EXPECT_GT(transform->position.y, 1.0F);
  EXPECT_GT(transform->position.x, 0.5F);
}

TEST(WorldEntityTests, UnmountedLivingMountConsumesMovementIntentButOccupiedMountDoesNot) {
  const arv::entity::VehicleDefinitionRegistry vehicles =
      make_test_living_mount_registry();
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone =
      registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  place_test_floor(world, stone, -2, 6, -2, 2);

  arv::entity::EntityComponentSet components;
  components.collision = arv::entity::AuthoredCollisionComponent{{1.4F, 1.6F, 1.4F}};
  components.movement = arv::entity::AuthoredMovementComponent{4.0F};
  components.rideable = arv::entity::AuthoredRideableComponent{"arvane:test_horse"};
  components.vehicle_physics =
      arv::entity::AuthoredVehiclePhysicsComponent{"arvane:test_horse"};
  components.brain = arv::entity::AuthoredBrainComponent{};
  components.brain->tick_interval = 1U;
  components.brain->goals.push_back({.type = "arvane:wander",
                                     .priority = 1,
                                     .speed_multiplier = 1.0F,
                                     .duration_ticks = 20U,
                                     .animation = "walk"});
  arv::world::EntityGoalRegistry goals = make_base_goal_registry();

  arv::world::EntityCreateInfo first_info;
  first_info.type_name = "test:horse";
  first_info.position = {0.0F, 1.0F, 0.0F};
  first_info.authored_components = components;
  first_info.vehicle_definitions = &vehicles;
  const arv::EntityID unmounted = world.create_entity(std::move(first_info));
  ASSERT_NE(unmounted, (arv::EntityID{}));

  (void)world.tick_entities(&registry, &goals, &vehicles);
  const arv::entity::EntityTransform* unmounted_transform =
      world.find_entity_transform(unmounted);
  ASSERT_NE(unmounted_transform, nullptr);
  EXPECT_GT(
      arv::horizontal_length(
          unmounted_transform->position - arv::Vec3f{0.0F, 1.0F, 0.0F}),
      0.0F);

  arv::world::EntityCreateInfo occupied_info;
  occupied_info.type_name = "test:horse";
  occupied_info.position = {3.0F, 1.0F, 0.0F};
  occupied_info.authored_components = components;
  occupied_info.vehicle_definitions = &vehicles;
  const arv::EntityID occupied = world.create_entity(std::move(occupied_info));
  ASSERT_NE(occupied, (arv::EntityID{}));
  const arv::EntityID passenger = world.create_entity();
  ASSERT_TRUE(world.mount_entity(occupied, passenger, std::string_view{"saddle"}));

  (void)world.tick_entities(&registry, &goals, &vehicles);
  const arv::entity::EntityTransform* occupied_transform =
      world.find_entity_transform(occupied);
  ASSERT_NE(occupied_transform, nullptr);
  EXPECT_FLOAT_EQ(occupied_transform->position.x, 3.0F);
}

TEST(WorldEntityTests, HealthDamageAndInvulnerabilityAreCentralized) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::world::EntityCreateInfo source_info;
  source_info.position = {0.0F, 0.0F, 0.0F};
  const arv::EntityID source = world.create_entity(std::move(source_info));

  arv::entity::EntityComponentSet components;
  components.health = arv::entity::AuthoredHealthComponent{
      10.0F,
      8.0F,
      false};
  arv::world::EntityCreateInfo target_info;
  target_info.position = {1.0F, 0.0F, 0.0F};
  target_info.authored_components = components;
  const arv::EntityID target = world.create_entity(std::move(target_info));

  const arv::world::DamageResult first = world.apply_damage({source,
                                                             target,
                                                             3.0F,
                                                             arv::world::DamageCause::EntityAttack});
  EXPECT_TRUE(first.applied);
  EXPECT_FALSE(first.killed);
  EXPECT_FLOAT_EQ(first.previous_health, 8.0F);
  EXPECT_FLOAT_EQ(first.new_health, 5.0F);

  arv::entity::EntityHealth* health = world.find_entity_health(target);
  ASSERT_NE(health, nullptr);
  EXPECT_FLOAT_EQ(health->current, 5.0F);
  health->invulnerable = true;

  const arv::world::DamageResult invulnerable = world.apply_damage({source,
                                                                    target,
                                                                    3.0F,
                                                                    arv::world::DamageCause::EntityAttack});
  EXPECT_FALSE(invulnerable.applied);
  EXPECT_FLOAT_EQ(invulnerable.new_health, 5.0F);

  health->invulnerable = false;
  const arv::world::DamageResult lethal = world.apply_damage({source,
                                                              target,
                                                              8.0F,
                                                              arv::world::DamageCause::EntityAttack});
  EXPECT_TRUE(lethal.applied);
  EXPECT_TRUE(lethal.killed);
  EXPECT_FLOAT_EQ(lethal.new_health, 0.0F);
  EXPECT_TRUE(world.entity_exists(target));
  world.flush_entity_despawns();
  EXPECT_FALSE(world.entity_exists(target));
}

TEST(WorldEntityTests, PlayerLethalDamageRespawnsAtWorldSpawnWithFullHealth) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.set_spawn_point(arv::Location{12.0F, 64.0F, -8.0F, 0.5F, -0.25F});
  const arv::EntityID source = world.create_entity();
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {2.0F, 20.0F, 2.0F};
  player.velocity = {1.0F, 2.0F, 3.0F};
  player.grounded = true;
  const std::optional<arv::EntityID> target =
      world.entity_for_player(arv::PlayerID{1});
  ASSERT_TRUE(target.has_value());
  arv::entity::EntityHealth* health = world.find_entity_health(target.value());
  ASSERT_NE(health, nullptr);
  health->current = 2.0F;

  const arv::world::DamageResult lethal = world.apply_damage({source,
                                                              target.value(),
                                                              3.0F,
                                                              arv::world::DamageCause::EntityAttack});

  EXPECT_TRUE(lethal.applied);
  EXPECT_TRUE(lethal.killed);
  EXPECT_FLOAT_EQ(lethal.new_health, health->max);
  EXPECT_FLOAT_EQ(health->current, health->max);
  EXPECT_EQ(player.position, (arv::Vec3f{12.0F, 64.0F, -8.0F}));
  EXPECT_EQ(player.velocity, (arv::Vec3f{}));
  EXPECT_FLOAT_EQ(player.yaw_radians, 0.5F);
  EXPECT_FLOAT_EQ(player.pitch_radians, -0.25F);
  EXPECT_FALSE(player.grounded);
  EXPECT_TRUE(world.entity_exists(target.value()));
}

TEST(WorldEntityTests, PlayerLethalDamageUsesCustomRespawnLocation) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.set_spawn_point(arv::Location{12.0F, 64.0F, -8.0F, 0.5F, -0.25F});
  const arv::EntityID source = world.create_entity();
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {2.0F, 20.0F, 2.0F};
  player.respawn_location =
      arv::Location{32.0F, 72.0F, 48.0F, -0.75F, 0.125F};
  const std::optional<arv::EntityID> target =
      world.entity_for_player(arv::PlayerID{1});
  ASSERT_TRUE(target.has_value());
  arv::entity::EntityHealth* health = world.find_entity_health(target.value());
  ASSERT_NE(health, nullptr);
  health->current = 2.0F;

  const arv::world::DamageResult lethal = world.apply_damage({source,
                                                              target.value(),
                                                              3.0F,
                                                              arv::world::DamageCause::EntityAttack});

  EXPECT_TRUE(lethal.applied);
  EXPECT_TRUE(lethal.killed);
  EXPECT_FLOAT_EQ(lethal.new_health, health->max);
  EXPECT_EQ(player.position, (arv::Vec3f{32.0F, 72.0F, 48.0F}));
  EXPECT_EQ(player.velocity, (arv::Vec3f{}));
  EXPECT_FLOAT_EQ(player.yaw_radians, -0.75F);
  EXPECT_FLOAT_EQ(player.pitch_radians, 0.125F);
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

TEST(WorldEntityTests, KnockbackVelocityMovesEntitiesSmoothlyAcrossTicks) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));

  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:test_knockback_entity";
  info.position = {0.5F, 1.0F, 0.5F};
  arv::entity::EntityComponentSet components;
  components.collision = arv::entity::AuthoredCollisionComponent{{0.9F, 0.9F, 0.9F}};
  info.authored_components = components;
  const arv::EntityID entity = world.create_entity(std::move(info));
  arv::entity::EntityVelocity* velocity = world.find_entity_velocity(entity);
  ASSERT_NE(velocity, nullptr);
  velocity->linear = {6.0F, 0.0F, 0.0F};

  (void)world.tick_entities(&registry);

  const arv::entity::EntityTransform* transform = world.find_entity_transform(entity);
  ASSERT_NE(transform, nullptr);
  const float first_x = transform->position.x;
  EXPECT_GT(first_x, 0.55F);
  EXPECT_LT(first_x, 0.7F);
  EXPECT_FLOAT_EQ(velocity->linear.x, 6.0F);

  (void)world.tick_entities(&registry);

  EXPECT_GT(transform->position.x, first_x);
  EXPECT_LT(transform->position.x, 0.8F);
}

TEST(WorldEntityTests, GoalDrivenEntitiesJumpWhenBlockedByOneBlockLedge) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  arv::world::EntityGoalRegistry goals = make_base_goal_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 1, 0}}, stone));

  const arv::EntityID entity = create_goal_driven_entity(world, {0.5F, 1.001F, 0.5F});

  (void)world.tick_entities(&registry, &goals);

  const arv::entity::EntityVelocity* velocity = world.find_entity_velocity(entity);
  ASSERT_NE(velocity, nullptr);
  EXPECT_GT(velocity->linear.y, 0.0F);
}

TEST(WorldEntityTests, GoalDrivenEntitiesDoNotJumpIntoTallWalls) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  arv::world::EntityGoalRegistry goals = make_base_goal_registry();
  const arv::voxel::BlockVariantID stone = registry.find_variant("arvane:stone").value();
  arv::world::WorldRuntime world{arv::WorldID{1}};
  (void)world.ensure_chunk({{0, 0, 0}});
  ASSERT_TRUE(world.set_block({{0, 0, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 1, 0}}, stone));
  ASSERT_TRUE(world.set_block({{1, 2, 0}}, stone));

  const arv::EntityID entity = create_goal_driven_entity(world, {0.5F, 1.001F, 0.5F});

  (void)world.tick_entities(&registry, &goals);

  const arv::entity::EntityVelocity* velocity = world.find_entity_velocity(entity);
  ASSERT_NE(velocity, nullptr);
  EXPECT_FLOAT_EQ(velocity->linear.y, 0.0F);

  const arv::entity::BrainComponent* brain = world.find_entity_brain(entity);
  ASSERT_NE(brain, nullptr);
  const arv::entity::GoalSlot* active_goal = first_active_goal(*brain);
  ASSERT_NE(active_goal, nullptr);
  const arv::Vec3f direction = active_goal->state.movement_direction;
  EXPECT_GT(direction.x * direction.x + direction.z * direction.z, 0.0F);
  EXPECT_NE(direction, (arv::Vec3f{1.0F, 0.0F, 0.0F}));

  const arv::entity::MovementIntent* intent = world.find_entity_movement_intent(entity);
  ASSERT_NE(intent, nullptr);
  EXPECT_EQ(intent->desired_direction, direction);
}

TEST(WorldEntityTests, GoalDrivenEntitiesPickOpenDirectionWhenOppositeWallIsBlocked) {
  const arv::voxel::BlockRegistry registry = arv::testsupport::make_test_block_registry();
  arv::world::EntityGoalRegistry goals = make_base_goal_registry();
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

  (void)world.tick_entities(&registry, &goals);

  const arv::entity::BrainComponent* brain = world.find_entity_brain(entity);
  ASSERT_NE(brain, nullptr);
  const arv::entity::GoalSlot* active_goal = first_active_goal(*brain);
  ASSERT_NE(active_goal, nullptr);
  EXPECT_NEAR(
      std::abs(active_goal->state.movement_direction.z),
      1.0F,
      0.15F);

  const arv::entity::EntityTransform* before = world.find_entity_transform(entity);
  ASSERT_NE(before, nullptr);
  const arv::f32 z_before = before->position.z;

  (void)world.tick_entities(&registry, &goals);

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
  EXPECT_NE(find_active_animation(*animation, "walk"), nullptr);

  player.velocity = {};
  player.swimming = true;
  (void)world.tick_entities();
  EXPECT_EQ(animation->active.size(), 1U);
  EXPECT_NE(find_active_animation(*animation, "swim_idle"), nullptr);

  player.velocity = {1.0F, 0.0F, 0.0F};
  (void)world.tick_entities();
  EXPECT_EQ(animation->active.size(), 1U);
  EXPECT_NE(find_active_animation(*animation, "swim"), nullptr);

  player.velocity = {};
  player.swimming = false;
  player.sneaking = true;
  (void)world.tick_entities();
  EXPECT_EQ(animation->active.size(), 1U);
  EXPECT_NE(find_active_animation(*animation, "sneak"), nullptr);

  ASSERT_NE(find_active_animation(*animation, "sneak"), nullptr);
  find_active_animation(*animation, "sneak")->time_seconds = 0.25F;
  (void)world.tick_entities();
  EXPECT_EQ(animation->active.size(), 1U);
  EXPECT_NE(find_active_animation(*animation, "sneak_idle"), nullptr);

  player.velocity = {0.0F, 0.0F, 1.0F};
  (void)world.tick_entities();
  EXPECT_EQ(animation->active.size(), 1U);
  EXPECT_NE(find_active_animation(*animation, "sneak_walk"), nullptr);

  ASSERT_NE(find_active_animation(*animation, "sneak_walk"), nullptr);
  find_active_animation(*animation, "sneak_walk")->time_seconds = 1.0F;
  (void)world.tick_entities();
  EXPECT_EQ(animation->active.size(), 1U);
  EXPECT_NE(find_active_animation(*animation, "sneak_walk"), nullptr);
}

TEST(WorldEntityTests, PlayerHitRightAnimationLayersWithLocomotionUntilItExpires) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{7});
  const std::optional<arv::EntityID> entity = world.entity_for_player(arv::PlayerID{7});
  ASSERT_TRUE(entity.has_value());

  player.velocity = {1.0F, 0.0F, 0.0F};
  ASSERT_TRUE(world.set_entity_animation(entity.value(), "hit_right"));
  arv::entity::EntityAnimationState* animation =
      world.find_entity_animation_state(entity.value());
  ASSERT_NE(animation, nullptr);
  ASSERT_NE(find_active_animation(*animation, "hit_right"), nullptr);
  find_active_animation(*animation, "hit_right")->time_seconds = 0.10F;

  (void)world.tick_entities();
  EXPECT_NE(find_active_animation(*animation, "walk"), nullptr);
  EXPECT_NE(find_active_animation(*animation, "hit_right"), nullptr);

  ASSERT_NE(find_active_animation(*animation, "hit_right"), nullptr);
  find_active_animation(*animation, "hit_right")->time_seconds = 0.25F;
  (void)world.tick_entities();
  EXPECT_NE(find_active_animation(*animation, "walk"), nullptr);
  EXPECT_EQ(find_active_animation(*animation, "hit_right"), nullptr);
}

TEST(WorldEntityTests, PlayerSneakAttackLayersHitWithSneakLocomotion) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{7});
  const std::optional<arv::EntityID> entity = world.entity_for_player(arv::PlayerID{7});
  ASSERT_TRUE(entity.has_value());

  player.sneaking = true;
  ASSERT_TRUE(world.set_entity_animation(entity.value(), "hit_left"));
  arv::entity::EntityAnimationState* animation =
      world.find_entity_animation_state(entity.value());
  ASSERT_NE(animation, nullptr);
  ASSERT_NE(find_active_animation(*animation, "hit_left"), nullptr);
  find_active_animation(*animation, "hit_left")->time_seconds = 0.10F;

  (void)world.tick_entities();
  EXPECT_NE(find_active_animation(*animation, "sneak"), nullptr);
  EXPECT_NE(find_active_animation(*animation, "hit_left"), nullptr);

  ASSERT_NE(find_active_animation(*animation, "hit_left"), nullptr);
  find_active_animation(*animation, "hit_left")->time_seconds = 0.25F;
  ASSERT_NE(find_active_animation(*animation, "sneak"), nullptr);
  find_active_animation(*animation, "sneak")->time_seconds = 0.25F;
  (void)world.tick_entities();
  EXPECT_NE(find_active_animation(*animation, "sneak_idle"), nullptr);
  EXPECT_EQ(find_active_animation(*animation, "hit_left"), nullptr);

  player.velocity = {0.0F, 0.0F, 1.0F};
  animation->active.push_back({"hit_right", 0.10F});
  (void)world.tick_entities();
  EXPECT_NE(find_active_animation(*animation, "sneak_walk"), nullptr);
  EXPECT_NE(find_active_animation(*animation, "hit_right"), nullptr);
}

TEST(WorldEntityTests, PersistentEntityRecordsRoundTripThroughWorldRuntime) {
  arv::world::WorldRuntime world{arv::WorldID{3}};
  arv::world::EntityCreateInfo info;
  info.type_name = "arvane:test_persistent";
  info.persistent = true;
  info.persistent_uuid = "entity-test-uuid";
  info.position = {4.0F, 5.0F, 6.0F};
  info.velocity = {0.5F, 0.0F, -0.5F};
  arv::entity::EntityComponentSet components;
  components.health = arv::entity::AuthoredHealthComponent{10.0F, 6.0F, true};
  info.authored_components = components;
  const arv::EntityID entity = world.create_entity(std::move(info));
  arv::entity::EntityHealth* health = world.find_entity_health(entity);
  ASSERT_NE(health, nullptr);
  health->current = 4.0F;

  const std::vector<arv::entity::PersistentEntityRecord> records =
      world.persistent_entity_records();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records.front().uuid, "entity-test-uuid");
  EXPECT_EQ(records.front().type_name, "arvane:test_persistent");
  EXPECT_TRUE(records.front().has_health);
  EXPECT_FLOAT_EQ(records.front().health_current, 4.0F);
  EXPECT_FLOAT_EQ(records.front().health_max, 10.0F);
  EXPECT_TRUE(records.front().health_invulnerable);

  arv::world::WorldRuntime loaded{arv::WorldID{3}};
  loaded.restore_persistent_entities(records);
  EXPECT_EQ(loaded.entity_count(), 1U);
  const std::vector<arv::entity::PersistentEntityRecord> loaded_records =
      loaded.persistent_entity_records();
  ASSERT_EQ(loaded_records.size(), 1U);
  EXPECT_EQ(loaded_records.front().uuid, "entity-test-uuid");
  EXPECT_EQ(loaded_records.front().position, (arv::Vec3f{4.0F, 5.0F, 6.0F}));
  ASSERT_TRUE(loaded_records.front().has_health);
  EXPECT_FLOAT_EQ(loaded_records.front().health_current, 4.0F);
  EXPECT_FLOAT_EQ(loaded_records.front().health_max, 10.0F);
  EXPECT_TRUE(loaded_records.front().health_invulnerable);
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
