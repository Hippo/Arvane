#include "arv/asset/resource.hpp"
#include "arv/base/base_module.hpp"
#include "arv/entity/components.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

std::string read_text_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream output;
  output << input.rdbuf();
  return output.str();
}

} // namespace

TEST(EntityRegistryTests, BuildsBuiltinsAndConfiguredEntityTypes) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "entities": [
    {
      "name": "arvane:pig",
      "display_name": "Pig",
      "category": "passive",
      "components": {
        "arvane:transform": {},
        "arvane:collision": { "box": [0.9, 0.9, 0.9] },
        "arvane:movement": { "walk_speed": 1.2 },
        "arvane:health": { "max": 10, "current": 6, "invulnerable": true },
        "arvane:brain": {
          "behavior": "arvane:pig_behavior",
          "tick_rate": 5,
          "goals": [
            {
              "type": "arvane:wander",
              "priority": 8,
              "radius": 12,
              "speed_multiplier": 0.8,
              "animation": "walking"
            },
            {
              "type": "arvane:idle",
              "priority": 10
            }
          ]
        }
      },
      "presentation": {
        "model": "pig.json",
        "animation": "arvane:animation/pig"
      },
      "spawnable": true,
      "persistent_by_default": true,
      "replicated_by_default": true
    }
  ]
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;

  auto registry_result =
      arv::base::build_base_entity_registry(std::move(parsed.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  const arv::entity::EntityRegistry registry = std::move(registry_result.value());

  const arv::entity::EntityTypeRecord* player =
      registry.type(arv::entity::PlayerEntityTypeID);
  ASSERT_NE(player, nullptr);
  EXPECT_EQ(player->name, arv::entity::PlayerEntityTypeName);
  EXPECT_FALSE(player->spawnable);

  const std::optional<arv::EntityTypeID> pig_id = registry.find_type("arvane:pig");
  ASSERT_TRUE(pig_id.has_value());
  const arv::entity::EntityTypeRecord* pig = registry.type(pig_id.value());
  ASSERT_NE(pig, nullptr);
  EXPECT_EQ(pig->display_name, "Pig");
  EXPECT_EQ(pig->category, "passive");
  EXPECT_EQ(pig->model_asset, "arvane:model/entity/pig.json");
  EXPECT_EQ(pig->animation_asset, "arvane:animation/pig");
  ASSERT_TRUE(pig->components.collision.has_value());
  EXPECT_EQ(pig->components.collision->box, (arv::Vec3f{0.9F, 0.9F, 0.9F}));
  ASSERT_TRUE(pig->components.movement.has_value());
  EXPECT_FLOAT_EQ(pig->components.movement->walk_speed, 1.2F);
  ASSERT_TRUE(pig->components.health.has_value());
  EXPECT_FLOAT_EQ(pig->components.health->max, 10.0F);
  ASSERT_TRUE(pig->components.health->current.has_value());
  EXPECT_FLOAT_EQ(pig->components.health->current.value(), 6.0F);
  EXPECT_TRUE(pig->components.health->invulnerable);
  ASSERT_TRUE(pig->components.brain.has_value());
  EXPECT_EQ(pig->components.brain->behavior, "arvane:pig_behavior");
  EXPECT_EQ(pig->components.brain->tick_interval, 5U);
  ASSERT_EQ(pig->components.brain->goals.size(), 2U);
  EXPECT_EQ(pig->components.brain->goals.front().type, "arvane:wander");
  EXPECT_EQ(pig->components.brain->goals.front().animation, "walking");
  EXPECT_TRUE(pig->spawnable);
  EXPECT_TRUE(pig->persistent_by_default);
  EXPECT_TRUE(pig->replicated_by_default);

  const arv::asset::ResourceManager resources("/tmp/arvane_assets");
  EXPECT_EQ(
      resources.resolve_asset_path({pig->model_asset}, arv::asset::AssetKind::Model),
      std::filesystem::path{"/tmp/arvane_assets/models/entity/pig.json"});
  EXPECT_EQ(
      resources.resolve_asset_path({pig->animation_asset}, arv::asset::AssetKind::Animation),
      std::filesystem::path{"/tmp/arvane_assets/animations/pig.json"});
}

TEST(EntityRegistryTests, AcceptsSingleEntityDefinitionObject) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "display_name": "Pig",
  "presentation": {
    "model": "pig.json"
  }
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_EQ(parsed.value().size(), 1U);
  EXPECT_EQ(parsed.value().front().name, "arvane:pig");
  EXPECT_EQ(parsed.value().front().model_asset, "pig.json");
  EXPECT_TRUE(parsed.value().front().animation_asset.empty());
}

TEST(EntityRegistryTests, ParsesOptionalPresentationAnimation) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "presentation": {
    "model": "arvane:model/entity/pig",
    "animation": "arvane:animation/pig"
  }
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;
  auto registry_result =
      arv::base::build_base_entity_registry(std::move(parsed.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  const std::optional<arv::EntityTypeID> pig_id =
      registry_result.value().find_type("arvane:pig");
  ASSERT_TRUE(pig_id.has_value());
  const arv::entity::EntityTypeRecord* pig = registry_result.value().type(pig_id.value());
  ASSERT_NE(pig, nullptr);
  EXPECT_EQ(pig->animation_asset, "arvane:animation/pig");
}

TEST(EntityRegistryTests, ParsesExtendedGoalFields) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "test:hostile_pig",
  "components": {
    "arvane:brain": {
      "goals": [
        {
          "type": "arvane:melee_attack",
          "priority": 2,
          "range": 1.7,
          "damage": 3.5,
          "cooldown_ticks": 12,
          "memory_ticks": 80,
          "require_line_of_sight": true,
          "target": "nearest_player",
          "animation": "walk",
          "attack_animation": "bite"
        }
      ]
    }
  }
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;
  ASSERT_TRUE(parsed.value().front().components.brain.has_value());
  ASSERT_EQ(parsed.value().front().components.brain->goals.size(), 1U);

  const arv::entity::AuthoredGoalDefinition& goal =
      parsed.value().front().components.brain->goals.front();
  EXPECT_EQ(goal.type, "arvane:melee_attack");
  EXPECT_FLOAT_EQ(goal.range, 1.7F);
  EXPECT_FLOAT_EQ(goal.damage, 3.5F);
  EXPECT_EQ(goal.cooldown_ticks, 12U);
  EXPECT_EQ(goal.memory_ticks, 80U);
  EXPECT_TRUE(goal.require_line_of_sight);
  EXPECT_EQ(goal.target, "nearest_player");
  EXPECT_EQ(goal.animation, "walk");
  EXPECT_EQ(goal.attack_animation, "bite");
}

TEST(EntityRegistryTests, LoadsFirstPartyHorseBrainAndAnimation) {
  auto parsed = arv::entity::parse_entity_definitions_json(
      read_text_file("arvane_home/packs/arvane_base/data/arvane/entities.json"));
  ASSERT_TRUE(parsed) << parsed.error().message;
  auto registry_result =
      arv::base::build_base_entity_registry(std::move(parsed.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;

  const std::optional<arv::EntityTypeID> horse_id =
      registry_result.value().find_type("arvane:horse");
  ASSERT_TRUE(horse_id.has_value());
  const arv::entity::EntityTypeRecord* horse =
      registry_result.value().type(horse_id.value());
  ASSERT_NE(horse, nullptr);
  EXPECT_EQ(horse->animation_asset, "arvane:animation/horse");
  ASSERT_TRUE(horse->components.brain.has_value());
  ASSERT_GE(horse->components.brain->goals.size(), 4U);
  EXPECT_EQ(horse->components.brain->goals[0].type, "arvane:swim_in_fluid");
  EXPECT_EQ(horse->components.brain->goals[1].type, "arvane:avoid_fluid");
  EXPECT_EQ(horse->components.brain->goals[2].type, "arvane:wander");
  EXPECT_EQ(horse->components.brain->goals[3].type, "arvane:idle");

  const std::optional<arv::EntityTypeID> pig_id =
      registry_result.value().find_type("arvane:pig");
  ASSERT_TRUE(pig_id.has_value());
  const arv::entity::EntityTypeRecord* pig =
      registry_result.value().type(pig_id.value());
  ASSERT_NE(pig, nullptr);
  ASSERT_TRUE(pig->components.brain.has_value());
  ASSERT_GE(pig->components.brain->goals.size(), 4U);
  EXPECT_EQ(pig->components.brain->goals[0].type, "arvane:swim_in_fluid");
  EXPECT_EQ(pig->components.brain->goals[1].type, "arvane:avoid_fluid");
  EXPECT_EQ(pig->components.brain->goals[2].type, "arvane:wander");
  EXPECT_EQ(pig->components.brain->goals[3].type, "arvane:idle");
}

TEST(EntityRegistryTests, RejectsInvalidPresentationAnimationAssetKind) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "presentation": {
    "model": "arvane:model/entity/pig",
    "animation": "arvane:model/entity/pig"
  }
}
)json");

  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("presentation.animation"), std::string::npos);
}

TEST(EntityRegistryTests, RejectsUnknownAuthoredComponentIDs) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "components": {
    "arvane:not_a_component": {}
  }
}
)json");

  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("unknown entity component ID"), std::string::npos);
}

TEST(EntityRegistryTests, RejectsUnknownBehaviorGoalIDs) {
  auto parsed = arv::entity::parse_entity_definitions_json(R"json(
{
  "name": "arvane:pig",
  "components": {
    "arvane:brain": {
      "goals": [
        { "type": "arvane:does_not_exist", "priority": 1 }
      ]
    }
  }
}
)json");

  ASSERT_TRUE(parsed) << parsed.error().message;
  auto registry_result =
      arv::base::build_base_entity_registry(std::move(parsed.value()));
  ASSERT_FALSE(registry_result);
  EXPECT_NE(registry_result.error().message.find("unknown behavior goal ID"), std::string::npos);
}

TEST(EntityRegistryTests, RejectsDuplicateExplicitEntityNames) {
  std::vector<arv::entity::EntityDefinition> definitions;
  definitions.push_back({.name = "arvane:pig"});
  definitions.push_back({.name = "arvane:pig"});

  auto registry_result =
      arv::base::build_base_entity_registry(std::move(definitions));
  ASSERT_FALSE(registry_result);
  EXPECT_NE(registry_result.error().message.find("duplicate"), std::string::npos);
}

TEST(EntityRegistryTests, KeepsPlayerDefinitionReservedWhenConfigured) {
  std::vector<arv::entity::EntityDefinition> definitions;
  arv::entity::EntityDefinition player;
  player.name = std::string{arv::entity::PlayerEntityTypeName};
  player.display_name = "Custom Player";
  player.spawnable = true;
  player.persistent_by_default = false;
  player.replicated_by_default = false;
  definitions.push_back(std::move(player));

  auto registry_result =
      arv::base::build_base_entity_registry(std::move(definitions));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  const arv::entity::EntityTypeRecord* player_record =
      registry_result.value().type(arv::entity::PlayerEntityTypeID);
  ASSERT_NE(player_record, nullptr);
  EXPECT_EQ(player_record->display_name, "Custom Player");
  EXPECT_FALSE(player_record->spawnable);
  EXPECT_TRUE(player_record->persistent_by_default);
  EXPECT_TRUE(player_record->replicated_by_default);
}

TEST(VehicleDefinitionRegistryTests, LoadsValidVehicleDefinitions) {
  auto parsed = arv::entity::parse_vehicle_definitions_json(R"json(
{
  "vehicles": [
    {
      "name": "arvane:buggy",
      "kind": "vehicle",
      "physics": "ground_vehicle",
      "collision": { "type": "aabb", "size": [1.8, 1.2, 3.2] },
      "seats": [
        {
          "id": "driver",
          "role": "driver",
          "local_position": [0.0, 0.8, 0.0],
          "local_rotation": [0.0, 0.0, 0.0, 1.0],
          "controls_vehicle": true,
          "camera_mode": "third_person_follow"
        }
      ],
      "movement": {
        "max_forward_speed": 24.0,
        "max_reverse_speed": 8.0,
        "acceleration": 18.0,
        "brake_force": 30.0,
        "drag": 0.92,
        "turn_rate": 80.0,
        "lateral_grip": 0.85,
        "gravity_scale": 1.0,
        "max_step_height": 0.5
      },
      "authority": {
        "max_speed": 28.0,
        "max_acceleration": 26.0,
        "max_deceleration": 45.0,
        "max_angular_speed": 130.0,
        "max_vertical_speed": 30.0,
        "max_step_height": 0.6,
        "requires_ground": true,
        "requires_water": false
      },
      "crash": {
        "bump_threshold": 3.0,
        "hard_impact_threshold": 9.0,
        "ejection_threshold": 18.0,
        "destruction_threshold": 26.0,
        "explosion_threshold": 30.0,
        "min_direct_impact_dot": 0.45,
        "event_cooldown_ticks": 30,
        "velocity_retention": 0.08,
        "ejection_velocity_scale": 0.55,
        "ejection_upward_velocity": 5.0,
        "ejection_min_horizontal_speed": 4.0,
        "ejection_max_horizontal_speed": 16.0,
        "ejection_max_vertical_speed": 9.0,
        "remount_cooldown_ticks": 50,
        "damage": {
          "hard_impact": 2.0,
          "ejection": 4.0,
          "destruction": 12.0
        }
      }
    }
  ]
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;
  auto registry_result = arv::entity::VehicleDefinitionRegistry::build(std::move(parsed.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;

  const arv::entity::VehicleDefinition* buggy =
      registry_result.value().find("arvane:buggy");
  ASSERT_NE(buggy, nullptr);
  EXPECT_EQ(buggy->physics, arv::entity::VehiclePhysicsKind::GroundVehicle);
  EXPECT_EQ(buggy->collision_size, (arv::Vec3f{1.8F, 1.2F, 3.2F}));
  ASSERT_EQ(buggy->seats.size(), 1U);
  EXPECT_TRUE(buggy->seats.front().controls_vehicle);
  EXPECT_FLOAT_EQ(buggy->ground.max_forward_speed, 24.0F);
  EXPECT_NEAR(buggy->ground.turn_rate, arv::degrees_to_radians(80.0F), 0.00001F);
  EXPECT_NEAR(
      buggy->authority.max_angular_speed,
      arv::degrees_to_radians(130.0F),
      0.00001F);
  EXPECT_TRUE(buggy->authority.requires_ground);
  EXPECT_TRUE(buggy->crash.enabled);
  EXPECT_FLOAT_EQ(buggy->crash.hard_impact_threshold, 9.0F);
  EXPECT_FLOAT_EQ(buggy->crash.ejection_threshold, 18.0F);
  EXPECT_FLOAT_EQ(buggy->crash.explosion_threshold, 30.0F);
  EXPECT_FLOAT_EQ(buggy->crash.min_direct_impact_dot, 0.45F);
  EXPECT_EQ(buggy->crash.event_cooldown_ticks, 30U);
  EXPECT_FLOAT_EQ(buggy->crash.ejection_max_horizontal_speed, 16.0F);
  EXPECT_FLOAT_EQ(buggy->crash.destruction_damage, 12.0F);
}

TEST(VehicleDefinitionRegistryTests, BaseBuggyCrashDestructionIsInsideAuthorityEnvelope) {
  auto parsed = arv::entity::parse_vehicle_definitions_json(
      read_text_file("arvane_home/packs/arvane_base/data/arvane/vehicles.json"));
  ASSERT_TRUE(parsed) << parsed.error().message;
  auto registry_result = arv::entity::VehicleDefinitionRegistry::build(std::move(parsed.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;

  const arv::entity::VehicleDefinition* buggy =
      registry_result.value().find("arvane:buggy");
  ASSERT_NE(buggy, nullptr);
  ASSERT_TRUE(buggy->crash.enabled);
  EXPECT_GT(buggy->crash.destruction_threshold, buggy->crash.ejection_threshold);
  EXPECT_LE(buggy->crash.destruction_threshold, buggy->authority.max_speed);
  EXPECT_LE(buggy->crash.explosion_threshold, buggy->authority.max_speed);
  EXPECT_TRUE(buggy->crash.destroy_on_destruction);
  EXPECT_TRUE(buggy->crash.explode_on_destruction);
}

TEST(VehicleDefinitionRegistryTests, LoadsAircraftAndSubmersibleDefinitions) {
  auto parsed = arv::entity::parse_vehicle_definitions_json(R"json(
{
  "vehicles": [
    {
      "name": "arvane:plane",
      "kind": "vehicle",
      "physics": "aircraft",
      "collision": { "type": "aabb", "size": [4.0, 1.5, 3.0625] },
      "seats": [
        {
          "id": "pilot",
          "role": "driver",
          "local_position": [0.0, 0.75, 0.35],
          "local_rotation": [0.0, 0.0, 0.0, 1.0],
          "controls_vehicle": true,
          "camera_mode": "vehicle_chase"
        }
      ],
      "movement": {
        "min_speed": 0.0,
        "max_speed": 42.0,
        "acceleration": 18.0,
        "brake_deceleration": 24.0,
        "drag": 0.985,
        "pitch_rate": 75.0,
        "yaw_rate": 55.0,
        "roll_rate": 110.0,
        "roll_auto_level_rate": 60.0,
        "lift_strength": 24.0,
        "stall_speed": 9.0,
        "stall_gravity_scale": 1.8,
        "gravity_scale": 0.65,
        "max_climb_rate": 18.0,
        "max_descent_rate": 28.0,
        "ground_takeoff_speed": 10.0,
        "ground_drag": 0.92
      },
      "authority": {
        "max_speed": 52.0,
        "max_acceleration": 36.0,
        "max_deceleration": 48.0,
        "max_angular_speed": 180.0,
        "max_vertical_speed": 36.0,
        "requires_ground": false,
        "requires_water": false,
        "requires_swept_collision": true,
        "max_unloaded_chunk_entry_distance": 0.0
      }
    },
    {
      "name": "arvane:submarine",
      "kind": "vehicle",
      "physics": "submersible",
      "collision": { "type": "aabb", "size": [2.0, 1.5, 2.25] },
      "seats": [
        {
          "id": "pilot",
          "role": "driver",
          "local_position": [0.0, 0.85, 0.3],
          "local_rotation": [0.0, 0.0, 0.0, 1.0],
          "controls_vehicle": true,
          "camera_mode": "vehicle_chase"
        }
      ],
      "movement": {
        "max_forward_speed": 24.0,
        "max_reverse_speed": 8.0,
        "acceleration": 28.0,
        "brake_deceleration": 24.0,
        "turn_rate": 65.0,
        "pitch_rate": 35.0,
        "ascend_rate": 10.0,
        "descend_rate": 10.0,
        "water_drag": 0.97,
        "air_drag": 0.45,
        "gravity_scale_out_of_water": 1.0,
        "min_water_occupancy": 0.45,
        "preferred_water_occupancy": 0.75,
        "surface_float_offset": 0.15
      },
      "authority": {
        "max_speed": 28.0,
        "max_acceleration": 36.0,
        "max_deceleration": 36.0,
        "max_angular_speed": 110.0,
        "max_vertical_speed": 14.0,
        "requires_ground": false,
        "requires_water": false,
        "min_water_occupancy": 0.35
      }
    }
  ]
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;
  auto registry_result = arv::entity::VehicleDefinitionRegistry::build(std::move(parsed.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;

  const arv::entity::VehicleDefinition* plane =
      registry_result.value().find("arvane:plane");
  ASSERT_NE(plane, nullptr);
  EXPECT_EQ(plane->physics, arv::entity::VehiclePhysicsKind::Aircraft);
  ASSERT_EQ(plane->seats.size(), 1U);
  EXPECT_EQ(plane->seats.front().camera_mode, arv::entity::CameraMode::VehicleChase);
  EXPECT_FLOAT_EQ(plane->aircraft.max_speed, 42.0F);
  EXPECT_NEAR(plane->aircraft.roll_rate, arv::degrees_to_radians(110.0F), 0.00001F);
  EXPECT_TRUE(plane->authority.requires_swept_collision);
  EXPECT_FLOAT_EQ(plane->authority.max_unloaded_chunk_entry_distance, 0.0F);

  const arv::entity::VehicleDefinition* submarine =
      registry_result.value().find("arvane:submarine");
  ASSERT_NE(submarine, nullptr);
  EXPECT_EQ(submarine->physics, arv::entity::VehiclePhysicsKind::Submersible);
  EXPECT_FLOAT_EQ(submarine->submersible.max_forward_speed, 24.0F);
  EXPECT_NEAR(submarine->submersible.turn_rate, arv::degrees_to_radians(65.0F), 0.00001F);
  EXPECT_FLOAT_EQ(submarine->submersible.min_water_occupancy, 0.45F);
  EXPECT_FLOAT_EQ(submarine->authority.min_water_occupancy, 0.35F);
}

TEST(VehicleDefinitionRegistryTests, RejectsMissingControllingSeat) {
  auto parsed = arv::entity::parse_vehicle_definitions_json(R"json(
{
  "name": "arvane:boat",
  "kind": "vehicle",
  "physics": "surface_water_vehicle",
  "collision": { "type": "aabb", "size": [1.0, 1.0, 1.0] },
  "seats": [
    {
      "id": "passenger",
      "role": "passenger",
      "local_position": [0.0, 0.0, 0.0],
      "local_rotation": [0.0, 0.0, 0.0, 1.0],
      "controls_vehicle": false,
      "camera_mode": "third_person_follow"
    }
  ],
  "movement": {
    "acceleration": 1.0,
    "max_speed": 2.0,
    "turn_rate": 3.0,
    "water_drag": 0.8,
    "land_drag": 0.2,
    "float_height_offset": 0.0,
    "buoyancy_snap_speed": 1.0
  },
  "authority": {
    "max_speed": 3.0,
    "max_acceleration": 4.0,
    "max_deceleration": 5.0,
    "max_angular_speed": 6.0,
    "max_vertical_speed": 7.0,
    "requires_ground": false,
    "requires_water": false
  }
}
)json");
  ASSERT_TRUE(parsed) << parsed.error().message;
  auto registry_result = arv::entity::VehicleDefinitionRegistry::build(std::move(parsed.value()));
  ASSERT_FALSE(registry_result);
  EXPECT_NE(registry_result.error().message.find("controlling seat"), std::string::npos);
}

TEST(VehicleDefinitionRegistryTests, RejectsInvalidCollisionSize) {
  auto parsed = arv::entity::parse_vehicle_definitions_json(R"json(
{
  "name": "arvane:bad_vehicle",
  "kind": "vehicle",
  "physics": "ground_vehicle",
  "collision": { "type": "aabb", "size": [0.0, 1.0, 1.0] },
  "seats": []
}
)json");
  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("collision.size"), std::string::npos);
}

TEST(VehicleDefinitionRegistryTests, RejectsInvalidCrashThresholdOrdering) {
  auto parsed = arv::entity::parse_vehicle_definitions_json(R"json(
{
  "name": "arvane:bad_buggy",
  "kind": "vehicle",
  "physics": "ground_vehicle",
  "collision": { "type": "aabb", "size": [1.0, 1.0, 1.0] },
  "seats": [
    {
      "id": "driver",
      "role": "driver",
      "local_position": [0.0, 0.0, 0.0],
      "local_rotation": [0.0, 0.0, 0.0, 1.0],
      "controls_vehicle": true,
      "camera_mode": "third_person_follow"
    }
  ],
  "movement": {
    "max_forward_speed": 1.0,
    "max_reverse_speed": 1.0,
    "acceleration": 1.0,
    "brake_force": 1.0,
    "drag": 1.0,
    "turn_rate": 1.0,
    "lateral_grip": 1.0,
    "gravity_scale": 1.0,
    "max_step_height": 1.0
  },
  "authority": {
    "max_speed": 1.0,
    "max_acceleration": 1.0,
    "max_deceleration": 1.0,
    "max_angular_speed": 1.0,
    "max_vertical_speed": 1.0,
    "requires_ground": false,
    "requires_water": false
  },
  "crash": {
    "bump_threshold": 3.0,
    "hard_impact_threshold": 9.0,
    "ejection_threshold": 18.0,
    "destruction_threshold": 12.0
  }
}
)json");
  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("destruction_threshold"), std::string::npos);
}

TEST(VehicleDefinitionRegistryTests, RejectsUnknownPhysicsKind) {
  auto parsed = arv::entity::parse_vehicle_definitions_json(R"json(
{
  "name": "arvane:plane",
  "kind": "vehicle",
  "physics": "hovercraft",
  "collision": { "type": "aabb", "size": [1.0, 1.0, 1.0] },
  "seats": []
}
)json");
  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("unknown physics kind"), std::string::npos);
}

TEST(VehicleDefinitionRegistryTests, RejectsInvalidAircraftSpeedEnvelope) {
  auto parsed = arv::entity::parse_vehicle_definitions_json(R"json(
{
  "name": "arvane:bad_plane",
  "kind": "vehicle",
  "physics": "aircraft",
  "collision": { "type": "aabb", "size": [1.0, 1.0, 1.0] },
  "seats": [
    {
      "id": "pilot",
      "role": "driver",
      "local_position": [0.0, 0.0, 0.0],
      "local_rotation": [0.0, 0.0, 0.0, 1.0],
      "controls_vehicle": true,
      "camera_mode": "vehicle_chase"
    }
  ],
  "movement": {
    "min_speed": 12.0,
    "max_speed": 4.0,
    "acceleration": 1.0,
    "brake_deceleration": 1.0,
    "drag": 0.9,
    "pitch_rate": 1.0,
    "yaw_rate": 1.0,
    "roll_rate": 1.0,
    "roll_auto_level_rate": 1.0,
    "lift_strength": 1.0,
    "stall_speed": 1.0,
    "stall_gravity_scale": 1.0,
    "gravity_scale": 1.0,
    "max_climb_rate": 1.0,
    "max_descent_rate": 1.0,
    "ground_takeoff_speed": 1.0,
    "ground_drag": 0.9
  },
  "authority": {
    "max_speed": 1.0,
    "max_acceleration": 1.0,
    "max_deceleration": 1.0,
    "max_angular_speed": 1.0,
    "max_vertical_speed": 1.0,
    "requires_ground": false,
    "requires_water": false
  }
}
)json");
  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("min_speed"), std::string::npos);
}

TEST(VehicleDefinitionRegistryTests, RejectsInvalidSubmersibleWaterOccupancy) {
  auto parsed = arv::entity::parse_vehicle_definitions_json(R"json(
{
  "name": "arvane:bad_submarine",
  "kind": "vehicle",
  "physics": "submersible",
  "collision": { "type": "aabb", "size": [1.0, 1.0, 1.0] },
  "seats": [
    {
      "id": "pilot",
      "role": "driver",
      "local_position": [0.0, 0.0, 0.0],
      "local_rotation": [0.0, 0.0, 0.0, 1.0],
      "controls_vehicle": true,
      "camera_mode": "vehicle_chase"
    }
  ],
  "movement": {
    "max_forward_speed": 1.0,
    "max_reverse_speed": 1.0,
    "acceleration": 1.0,
    "brake_deceleration": 1.0,
    "turn_rate": 1.0,
    "pitch_rate": 1.0,
    "ascend_rate": 1.0,
    "descend_rate": 1.0,
    "water_drag": 0.9,
    "air_drag": 0.5,
    "gravity_scale_out_of_water": 1.0,
    "min_water_occupancy": 0.8,
    "preferred_water_occupancy": 0.4,
    "surface_float_offset": 0.0
  },
  "authority": {
    "max_speed": 1.0,
    "max_acceleration": 1.0,
    "max_deceleration": 1.0,
    "max_angular_speed": 1.0,
    "max_vertical_speed": 1.0,
    "requires_ground": false,
    "requires_water": false
  }
}
)json");
  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("min_water_occupancy"), std::string::npos);
}

TEST(VehicleDefinitionRegistryTests, RejectsMissingAuthorityValues) {
  auto parsed = arv::entity::parse_vehicle_definitions_json(R"json(
{
  "name": "arvane:buggy",
  "kind": "vehicle",
  "physics": "ground_vehicle",
  "collision": { "type": "aabb", "size": [1.0, 1.0, 1.0] },
  "seats": [
    {
      "id": "driver",
      "role": "driver",
      "local_position": [0.0, 0.0, 0.0],
      "local_rotation": [0.0, 0.0, 0.0, 1.0],
      "controls_vehicle": true,
      "camera_mode": "third_person_follow"
    }
  ],
  "movement": {
    "max_forward_speed": 1.0,
    "max_reverse_speed": 1.0,
    "acceleration": 1.0,
    "brake_force": 1.0,
    "drag": 1.0,
    "turn_rate": 1.0,
    "lateral_grip": 1.0,
    "gravity_scale": 1.0,
    "max_step_height": 1.0
  },
  "authority": {
    "max_acceleration": 1.0,
    "max_deceleration": 1.0,
    "max_angular_speed": 1.0,
    "max_vertical_speed": 1.0,
    "requires_ground": true,
    "requires_water": false
  }
}
)json");
  ASSERT_FALSE(parsed);
  EXPECT_NE(parsed.error().message.find("max_speed"), std::string::npos);
}
