#include "arv/asset/pack.hpp"
#include "arv/base/base_module.hpp"
#include "arv/client/client_runtime.hpp"
#include "arv/entity/components.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/server/server_runtime.hpp"
#include "arv/voxel/block.hpp"
#include "arv/voxel/light.hpp"
#include "arv/world/natural_spawning.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"
#include "arv/world/worldgen/loader.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr std::string_view TestBiomeID = "test:worldgen/biome/plains";
constexpr std::string_view GrassSpawnTag = "test:spawn/grass_like";

arv::voxel::BlockRegistry make_spawn_block_registry() {
  std::vector<arv::voxel::BlockDefinition> definitions;
  definitions.push_back(arv::voxel::make_air_definition());
  arv::voxel::BlockDefinition grass =
      arv::voxel::make_full_cube_definition("test:grass_block");
  grass.tags.push_back(std::string{GrassSpawnTag});
  definitions.push_back(std::move(grass));
  arv::voxel::BlockDefinition stone =
      arv::voxel::make_full_cube_definition("test:stone");
  stone.tags.push_back("test:spawn/stone");
  definitions.push_back(std::move(stone));
  arv::voxel::BlockDefinition water;
  water.name = "test:water";
  water.render_category = arv::voxel::RenderCategory::Transparent;
  water.properties.fluid = true;
  water.properties.replaceable = true;
  water.is_item = false;
  definitions.push_back(std::move(water));
  arv::voxel::BlockDefinition short_grass;
  short_grass.name = "test:short_grass";
  short_grass.render_category = arv::voxel::RenderCategory::Transparent;
  short_grass.properties.replaceable = true;
  short_grass.properties.full_cube = false;
  short_grass.is_item = false;
  definitions.push_back(std::move(short_grass));

  auto result = arv::voxel::BlockRegistry::build(std::move(definitions));
  if (!result) {
    throw std::runtime_error(result.error().message);
  }
  return std::move(result.value());
}

arv::entity::EntityRegistry make_spawn_entity_registry() {
  arv::entity::EntityDefinition pig;
  pig.name = "test:pig";
  pig.category = "passive";
  pig.spawnable = true;
  pig.persistent_by_default = true;
  pig.components.collision = arv::entity::AuthoredCollisionComponent{{0.9F, 0.9F, 0.9F}};
  pig.components.health = arv::entity::AuthoredHealthComponent{10.0F};

  arv::entity::EntityDefinition bat;
  bat.name = "test:bat";
  bat.category = "ambient";
  bat.spawnable = true;
  bat.persistent_by_default = false;

  arv::entity::EntityDefinition statue;
  statue.name = "test:statue";
  statue.spawnable = false;

  auto result = arv::entity::EntityRegistry::build({std::move(pig), std::move(bat), std::move(statue)});
  if (!result) {
    throw std::runtime_error(result.error().message);
  }
  return std::move(result.value());
}

arv::world::worldgen::BiomeSpawnEntryConfig make_spawn_config(
    std::string entity,
    arv::world::SpawnCategory category = arv::world::SpawnCategory::Passive,
    arv::u32 weight = 10U) {
  arv::world::worldgen::BiomeSpawnEntryConfig spawn;
  spawn.id = {"test:" + entity.substr(entity.find(':') + 1) + "_grass"};
  spawn.entity = {std::move(entity)};
  spawn.category = category;
  spawn.weight = weight;
  spawn.min_group = 1U;
  spawn.max_group = 1U;
  spawn.conditions.requires_surface = true;
  spawn.conditions.min_distance_from_player_blocks = 0U;
  spawn.conditions.max_distance_from_player_blocks = 96U;
  spawn.conditions.block_tags_below.push_back({std::string{GrassSpawnTag}});
  return spawn;
}

arv::world::SpawnBudgetProfileConfig make_test_budget_profile(
    arv::world::SpawnCategory category = arv::world::SpawnCategory::Passive) {
  arv::world::SpawnBudgetProfileConfig budget;
  budget.id = "test:worldgen/spawn_budget_profile/" +
              std::string{arv::world::spawn_category_name(category)};
  budget.category = category;
  budget.tick_interval = 1U;
  budget.attempts_per_interval = 64U;
  budget.max_successes_per_interval = 4U;
  budget.spawn_area.min_distance_blocks = 0U;
  budget.spawn_area.max_distance_blocks = 96U;
  budget.spawn_area.eligible_chunk_radius = 8U;
  budget.population.soft_cap_per_player = 32U;
  budget.population.hard_cap_per_player = 64U;
  budget.population.density_per_eligible_chunk = 1.0F;
  budget.population.max_per_chunk = 8U;
  budget.population.max_per_3x3_chunks = 32U;
  budget.credit.refill_per_interval = 128.0F;
  budget.credit.max_bank = 128.0F;
  budget.credit.spend_per_group_attempt = 1.0F;
  budget.credit.spend_per_entity_spawned = 1.0F;
  budget.despawn.enabled = false;
  budget.despawn.min_age_ticks = 0U;
  budget.despawn.distance_blocks = 192U;
  budget.despawn.only_if_over_soft_cap = false;
  budget.despawn.exclude_persistent = true;
  return budget;
}

std::shared_ptr<const arv::world::worldgen::ResolvedDimension> make_flat_spawn_dimension(
    std::vector<arv::world::worldgen::BiomeSpawnEntryConfig> spawns = {},
    std::optional<arv::world::SpawnBudgetProfileConfig> budget_override = std::nullopt) {
  namespace worldgen = arv::world::worldgen;

  auto dimension = std::make_shared<worldgen::ResolvedDimension>();
  dimension->raw.id = "test:worldgen/dimension/flat";
  dimension->raw.version = 1U;
  dimension->raw.seed_salt = "flat_spawn_test";
  dimension->raw.vertical.min_y = 0;
  dimension->raw.vertical.max_y = 31;
  dimension->raw.vertical.sea_level = -1;
  dimension->terrain.height.base = 0;
  dimension->terrain.height.scale = 0;
  dimension->terrain.height.detail_scale = 0;
  dimension->terrain.primary_noise.seed_salt = "flat_primary";
  dimension->terrain.primary_noise.amplitude = 0.0F;
  dimension->terrain.detail_noise.seed_salt = "flat_detail";
  dimension->terrain.detail_noise.amplitude = 0.0F;

  worldgen::BiomeConfig biome;
  biome.id = std::string{TestBiomeID};
  biome.spawns = std::move(spawns);
  for (const worldgen::BiomeSpawnEntryConfig& spawn : biome.spawns) {
    auto& category =
        biome.spawn_categories[
            arv::world::spawn_category_index(spawn.category)];
    if (!category.specified) {
      category.specified = true;
      category.budget =
          budget_override.has_value() && budget_override->category == spawn.category
              ? budget_override.value()
              : make_test_budget_profile(spawn.category);
      category.budget_profile = {category.budget.id};
    }
  }
  dimension->biomes.push_back({std::move(biome), std::nullopt});
  return dimension;
}

void fill_spawn_chunks(
    arv::world::WorldRuntime& world,
    const arv::voxel::BlockRegistry& registry) {
  const auto grass = registry.find_variant("test:grass_block");
  if (!grass.has_value()) {
    throw std::runtime_error("test grass block is missing");
  }
  for (arv::i32 chunk_z = -3; chunk_z <= 3; ++chunk_z) {
    for (arv::i32 chunk_x = -3; chunk_x <= 3; ++chunk_x) {
      (void)world.ensure_chunk_section(
          {{chunk_x, 0, chunk_z}},
          arv::voxel::ChunkLifecycleState::Active);
      for (arv::i32 local_z = 0; local_z < arv::ChunkSize; ++local_z) {
        for (arv::i32 local_x = 0; local_x < arv::ChunkSize; ++local_x) {
          world.set_block(
              {{
                  chunk_x * arv::ChunkSize + local_x,
                  0,
                  chunk_z * arv::ChunkSize + local_z,
              }},
              grass.value());
        }
      }
    }
  }
}

arv::world::WorldRuntime make_spawn_world(
    const arv::voxel::BlockRegistry& registry,
    std::shared_ptr<const arv::world::worldgen::ResolvedDimension> dimension =
        make_flat_spawn_dimension()) {
  arv::world::WorldRuntime world{arv::WorldID{1}};
  arv::world::WorldGenerationConfig config =
      arv::world::config_from_resolved_dimension(std::move(dimension));
  world.configure_height(arv::world::world_height_settings_from_generation_config(config));
  world.set_generation_context(42U, std::move(config));
  fill_spawn_chunks(world, registry);
  return world;
}

arv::world::SpawnEntry make_spawn_entry(
    const arv::entity::EntityRegistry& registry,
    arv::world::SpawnCategory category = arv::world::SpawnCategory::Passive) {
  const std::optional<arv::EntityTypeID> type_id = registry.find_type("test:pig");
  if (!type_id.has_value()) {
    throw std::runtime_error("test pig entity is missing");
  }
  const arv::entity::EntityTypeRecord* type = registry.type(type_id.value());
  if (type == nullptr) {
    throw std::runtime_error("test pig type record is missing");
  }
  arv::world::SpawnEntry entry;
  entry.id = "test:pig_grass";
  entry.entity_type = type_id.value();
  entry.entity_type_name = type->name;
  entry.category = category;
  entry.weight = 10U;
  entry.min_group = 1U;
  entry.max_group = 1U;
  entry.conditions.requires_surface = true;
  entry.conditions.min_distance_from_player_blocks = 0U;
  entry.conditions.max_distance_from_player_blocks = 96U;
  entry.conditions.block_tags_below.push_back(std::string{GrassSpawnTag});
  entry.persistent_by_default = type->persistent_by_default;
  entry.replicated_by_default = type->replicated_by_default;
  entry.authored_components = type->components;
  return entry;
}

arv::entity::EntityRegistry make_spawn_vehicle_entity_registry() {
  arv::entity::EntityDefinition horse;
  horse.name = "test:horse";
  horse.category = "passive";
  horse.spawnable = true;
  horse.persistent_by_default = true;
  horse.components.collision =
      arv::entity::AuthoredCollisionComponent{{1.4F, 1.6F, 2.0F}};
  horse.components.rideable =
      arv::entity::AuthoredRideableComponent{"test:horse"};
  horse.components.vehicle_physics =
      arv::entity::AuthoredVehiclePhysicsComponent{"test:horse"};

  auto result = arv::entity::EntityRegistry::build({std::move(horse)});
  if (!result) {
    throw std::runtime_error(result.error().message);
  }
  return std::move(result.value());
}

arv::entity::VehicleDefinitionRegistry make_spawn_vehicle_registry() {
  arv::entity::VehicleDefinition horse;
  horse.name = "test:horse";
  horse.kind = arv::entity::VehicleKind::Mount;
  horse.physics = arv::entity::VehiclePhysicsKind::LivingMount;
  horse.collision_size = {1.4F, 1.6F, 2.0F};
  horse.seats.push_back({"saddle",
                         "saddle",
                         arv::entity::SeatRole::Driver,
                         {0.0F, 1.1F, 0.0F},
                         {},
                         true,
                         arv::entity::CameraMode::ThirdPersonFollow});
  horse.living_mount = {
      4.0F,
      7.0F,
      12.0F,
      arv::degrees_to_radians(140.0F),
      5.0F,
      1.0F,
      0.6F,
      0.8F};
  horse.authority.max_speed = 8.0F;
  horse.authority.max_acceleration = 16.0F;
  horse.authority.max_deceleration = 22.0F;
  horse.authority.max_angular_speed = arv::degrees_to_radians(180.0F);
  horse.authority.max_vertical_speed = 8.0F;
  horse.authority.max_step_height = 0.6F;
  horse.authority.requires_ground = true;
  horse.authority.requires_water = false;

  auto result = arv::entity::VehicleDefinitionRegistry::build({std::move(horse)});
  if (!result) {
    throw std::runtime_error(result.error().message);
  }
  return std::move(result.value());
}

arv::world::SpawnEntry make_spawn_vehicle_entry(
    const arv::entity::EntityRegistry& registry) {
  const std::optional<arv::EntityTypeID> type_id = registry.find_type("test:horse");
  if (!type_id.has_value()) {
    throw std::runtime_error("test horse entity is missing");
  }
  const arv::entity::EntityTypeRecord* type = registry.type(type_id.value());
  if (type == nullptr) {
    throw std::runtime_error("test horse type record is missing");
  }
  arv::world::SpawnEntry entry;
  entry.id = "test:horse_grass";
  entry.entity_type = type_id.value();
  entry.entity_type_name = type->name;
  entry.category = arv::world::SpawnCategory::Passive;
  entry.weight = 10U;
  entry.min_group = 1U;
  entry.max_group = 1U;
  entry.conditions.requires_surface = true;
  entry.conditions.min_distance_from_player_blocks = 0U;
  entry.conditions.max_distance_from_player_blocks = 96U;
  entry.conditions.block_tags_below.push_back(std::string{GrassSpawnTag});
  entry.persistent_by_default = type->persistent_by_default;
  entry.replicated_by_default = type->replicated_by_default;
  entry.authored_components = type->components;
  return entry;
}

void set_body_light(
    arv::world::WorldRuntime& world,
    arv::BlockPos pos,
    arv::voxel::LightWord light) {
  arv::voxel::ChunkSection* section = world.find_chunk(arv::block_to_chunk(pos));
  ASSERT_NE(section, nullptr);
  ASSERT_TRUE(section->set_light(
      {{
          arv::floor_mod(pos.value.x, arv::ChunkSize),
          arv::floor_mod(pos.value.y, arv::ChunkSize),
          arv::floor_mod(pos.value.z, arv::ChunkSize),
      }},
      light));
}

void write_pack_manifest(
    const std::filesystem::path& packs_root,
    std::string_view id,
    std::string_view namespace_id) {
  arv::testsupport::write_text(
      packs_root / std::string{id} / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" +
          std::string{id} + "\"\n"
                            "display_name: \"" +
          std::string{id} + "\"\n"
                            "version: \"1.0.0\"\n"
                            "namespaces:\n"
                            "  - \"" +
          std::string{namespace_id} + "\"\n"
                                      "side: \"both\"\n"
                                      "dependencies: []\n"
                                      "load_after: []\n"
                                      "load_before: []\n"
                                      "capabilities:\n"
                                      "  assets: false\n"
                                      "  data: true\n"
                                      "  native_plugin: false\n");
}

arv::world::worldgen::RegistryResult load_spawn_loader_fixture(
    std::string_view condition_yaml,
    std::string_view table_yaml,
    std::string_view biome_spawns_yaml) {
  arv::testsupport::ScopedTempDir temp{"arvane_spawn_loader_"};
  const std::filesystem::path packs_root = temp.path / "packs";
  write_pack_manifest(packs_root, "bad_pack", "bad");
  if (!condition_yaml.empty()) {
    arv::testsupport::write_text(
        packs_root / "bad_pack" / "data" / "bad" / "worldgen" / "spawn_condition" /
            "grass.yaml",
        std::string{condition_yaml});
  }
  if (!table_yaml.empty()) {
    arv::testsupport::write_text(
        packs_root / "bad_pack" / "data" / "bad" / "worldgen" / "spawn_table" /
            "common.yaml",
        std::string{table_yaml});
  }
  arv::testsupport::write_text(
      packs_root / "bad_pack" / "data" / "bad" / "worldgen" / "spawn_budget_profile" /
          "passive.yaml",
      "category: passive\n"
      "tick_interval: 1\n"
      "attempts_per_interval: 4\n"
      "max_successes_per_interval: 1\n"
      "spawn_area:\n"
      "  min_distance_blocks: 0\n"
      "  max_distance_blocks: 64\n"
      "  eligible_chunk_radius: 4\n"
      "population:\n"
      "  soft_cap_per_player: 4\n"
      "  hard_cap_per_player: 8\n"
      "  density_per_eligible_chunk: 0.5\n"
      "  max_per_chunk: 2\n"
      "  max_per_3x3_chunks: 4\n"
      "credit:\n"
      "  refill_per_interval: 2\n"
      "  max_bank: 4\n"
      "  spend_per_group_attempt: 1\n"
      "  spend_per_entity_spawned: 1\n"
      "despawn:\n"
      "  enabled: true\n"
      "  min_age_ticks: 0\n"
      "  distance_blocks: 64\n"
      "  only_if_over_soft_cap: false\n"
      "  exclude_persistent: true\n");
  arv::testsupport::write_text(
      packs_root / "bad_pack" / "data" / "bad" / "worldgen" / "biome" / "bad.yaml",
      "id: bad:worldgen/biome/bad\n"
      "version: 1\n"
      "display_name: Bad\n"
      "spawns:\n" +
          std::string{biome_spawns_yaml});

  auto packs = arv::asset::PackManager::load_from_packs_root(packs_root);
  if (!packs) {
    return arv::world::worldgen::RegistryResult::failure(
        {"bad_pack", "packs", packs.error().describe()});
  }
  return arv::world::worldgen::load_worldgen_registry_from_packs(packs.value());
}

arv::voxel::BlockRegistry load_first_party_block_registry(
    const arv::asset::PackManager& packs) {
  auto block_definition_text = packs.load_merged_json_array_text("blocks", "blocks");
  if (!block_definition_text) {
    throw std::runtime_error(block_definition_text.error().describe());
  }
  auto definitions =
      arv::voxel::parse_block_definitions_json(block_definition_text.value());
  if (!definitions) {
    throw std::runtime_error(definitions.error().message);
  }

  arv::voxel::BlockBehaviorRegistry block_behaviors;
  arv::base::register_base_block_behaviors(block_behaviors);
  const auto block_behavior_exists = [&block_behaviors](std::string_view id) {
    return block_behaviors.contains(id);
  };
  auto registry = arv::voxel::BlockRegistry::build(
      std::move(definitions.value()),
      {.behavior_exists = arv::FunctionRef<bool(std::string_view)>{
           block_behavior_exists}});
  if (!registry) {
    throw std::runtime_error(registry.error().message);
  }
  return std::move(registry.value());
}

arv::entity::EntityRegistry load_first_party_entity_registry(
    const arv::asset::PackManager& packs) {
  auto entity_definition_text = packs.load_merged_json_array_text("entities", "entities");
  if (!entity_definition_text) {
    throw std::runtime_error(entity_definition_text.error().describe());
  }
  auto definitions =
      arv::entity::parse_entity_definitions_json(entity_definition_text.value());
  if (!definitions) {
    throw std::runtime_error(definitions.error().message);
  }
  auto registry =
      arv::base::build_base_entity_registry(std::move(definitions.value()));
  if (!registry) {
    throw std::runtime_error(registry.error().message);
  }
  return std::move(registry.value());
}

std::shared_ptr<const arv::world::worldgen::ResolvedDimension>
load_first_party_overworld_dimension(
    const arv::asset::PackManager& packs,
    const arv::voxel::BlockRegistry& blocks) {
  auto worldgen_registry =
      arv::world::worldgen::load_worldgen_registry_from_packs(packs);
  if (!worldgen_registry) {
    throw std::runtime_error(worldgen_registry.error().describe());
  }
  auto dimension = arv::world::worldgen::resolve_dimension(
      worldgen_registry.value(),
      "arvane:worldgen/dimension/overworld",
      blocks);
  if (!dimension) {
    throw std::runtime_error(dimension.error().describe());
  }
  return std::move(dimension.value());
}

void publish_generated_columns(
    arv::world::WorldRuntime& world,
    const std::shared_ptr<const arv::world::worldgen::ResolvedDimension>& dimension,
    arv::u64 seed,
    arv::ChunkCoord center,
    arv::i32 radius_chunks) {
  arv::world::WorldGenerationConfig config =
      arv::world::config_from_resolved_dimension(dimension);
  world.configure_height(arv::world::world_height_settings_from_generation_config(config));
  world.set_generation_context(seed, config);

  for (arv::i32 chunk_z = center.z - radius_chunks;
       chunk_z <= center.z + radius_chunks;
       ++chunk_z) {
    for (arv::i32 chunk_x = center.x - radius_chunks;
         chunk_x <= center.x + radius_chunks;
         ++chunk_x) {
      arv::world::ChunkGenerationRequest request;
      request.world_id = world.id();
      request.seed = seed;
      request.coord = {chunk_x, chunk_z};
      request.min_chunk_y = arv::floor_div(config.min_y, arv::ChunkSize);
      request.max_chunk_y = arv::floor_div(config.max_y, arv::ChunkSize);
      request.config = config;
      request.resolved_dimension = dimension;

      auto generated = arv::world::generate_chunk(request);
      ASSERT_TRUE(generated) << generated.error().message;
      ASSERT_TRUE(world.publish_generated_chunk(std::move(generated.value())))
          << "column=(" << chunk_x << "," << chunk_z << ")";
    }
  }
}

arv::u32 count_natural_entities(const arv::world::WorldRuntime& world) {
  arv::u32 count = 0U;
  for (const arv::EntityID id : world.entity_ids()) {
    if (world.find_entity_natural_spawn(id) != nullptr) {
      ++count;
    }
  }
  return count;
}

} // namespace

TEST(NaturalSpawnTypesTests, ParsesSpawnCategoriesAndTimePredicates) {
  EXPECT_EQ(
      arv::world::spawn_category_from_string("passive"),
      arv::world::SpawnCategory::Passive);
  EXPECT_EQ(
      arv::world::spawn_category_from_string("hostile"),
      arv::world::SpawnCategory::Hostile);
  EXPECT_FALSE(arv::world::spawn_category_from_string("boss").has_value());
  EXPECT_EQ(arv::world::spawn_category_name(arv::world::SpawnCategory::Water), "water");

  EXPECT_EQ(arv::world::time_predicate_from_string("day"), arv::world::TimePredicate::Day);
  EXPECT_EQ(arv::world::time_predicate_from_string("night"), arv::world::TimePredicate::Night);
  EXPECT_FALSE(arv::world::time_predicate_from_string("storm").has_value());
}

TEST(NaturalSpawnLoaderTests, ParsesFirstPartyBiomeSpawnTables) {
  auto packs = arv::asset::PackManager::load_from_packs_root("arvane_home/packs");
  ASSERT_TRUE(packs) << packs.error().describe();
  auto registry = arv::world::worldgen::load_worldgen_registry_from_packs(packs.value());
  ASSERT_TRUE(registry) << registry.error().describe();

  const auto condition =
      registry.value().spawn_conditions.find(
          "arvane:worldgen/spawn_condition/surface_grass_like");
  ASSERT_NE(condition, registry.value().spawn_conditions.end());
  EXPECT_TRUE(condition->second.requires_surface);
  ASSERT_EQ(condition->second.block_tags_below.size(), 1U);
  EXPECT_EQ(condition->second.block_tags_below.front().id, "arvane:spawn/grass_like");

  const auto table =
      registry.value().spawn_tables.find("arvane:worldgen/spawn_table/temperate_passive");
  ASSERT_NE(table, registry.value().spawn_tables.end());
  EXPECT_EQ(table->second.category, arv::world::SpawnCategory::Passive);
  ASSERT_EQ(table->second.entries.size(), 2U);
  EXPECT_EQ(table->second.entries[0].id.id, "arvane:pig_grass");
  EXPECT_EQ(table->second.entries[1].id.id, "arvane:horse_grass");
  EXPECT_EQ(table->second.entries[0].weight, 4U);
  EXPECT_EQ(table->second.entries[1].weight, 1U);
  EXPECT_EQ(table->second.entries[1].caps.max_near_player, 4U);
  EXPECT_EQ(table->second.entries[1].caps.max_per_chunk_area, 2U);

  const auto budget =
      registry.value().spawn_budget_profiles.find(
          "arvane:worldgen/spawn_budget_profile/passive_conservative");
  ASSERT_NE(budget, registry.value().spawn_budget_profiles.end());
  EXPECT_EQ(budget->second.category, arv::world::SpawnCategory::Passive);
  EXPECT_EQ(budget->second.tick_interval, 200U);
  EXPECT_EQ(budget->second.attempts_per_interval, 6U);
  EXPECT_EQ(budget->second.max_successes_per_interval, 1U);
  EXPECT_EQ(budget->second.population.soft_cap_per_player, 12U);
  EXPECT_EQ(budget->second.population.hard_cap_per_player, 20U);
  EXPECT_FLOAT_EQ(budget->second.population.density_per_eligible_chunk, 0.12F);
  EXPECT_EQ(budget->second.population.max_per_chunk, 4U);
  EXPECT_EQ(budget->second.population.max_per_3x3_chunks, 10U);
  EXPECT_TRUE(budget->second.despawn.enabled);
  EXPECT_EQ(budget->second.despawn.distance_blocks, 192U);

  const auto plains = registry.value().biomes.find("arvane:worldgen/biome/plains");
  ASSERT_NE(plains, registry.value().biomes.end());
  ASSERT_FALSE(plains->second.spawns.empty());
  const auto& plains_passive =
      plains->second.spawn_categories[
          arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)];
  EXPECT_TRUE(plains_passive.specified);
  EXPECT_EQ(
      plains_passive.budget_profile.id,
      "arvane:worldgen/spawn_budget_profile/passive_conservative");
  EXPECT_FLOAT_EQ(plains_passive.modifiers.density_multiplier, 1.0F);
  EXPECT_FLOAT_EQ(plains_passive.modifiers.soft_cap_multiplier, 1.0F);
  const auto pig = std::find_if(
      plains->second.spawns.begin(),
      plains->second.spawns.end(),
      [](const arv::world::worldgen::BiomeSpawnEntryConfig& spawn) {
        return spawn.id.id == "arvane:pig_grass" &&
               spawn.entity.id == "arvane:pig" &&
               spawn.category == arv::world::SpawnCategory::Passive;
      });
  ASSERT_NE(pig, plains->second.spawns.end());
  EXPECT_EQ(pig->weight, 4U);
  EXPECT_EQ(pig->min_group, 1U);
  EXPECT_EQ(pig->max_group, 2U);
  EXPECT_TRUE(pig->conditions.requires_surface);
  EXPECT_FALSE(pig->conditions.min_light.has_value());
  const auto horse = std::find_if(
      plains->second.spawns.begin(),
      plains->second.spawns.end(),
      [](const arv::world::worldgen::BiomeSpawnEntryConfig& spawn) {
        return spawn.id.id == "arvane:horse_grass" &&
               spawn.entity.id == "arvane:horse" &&
               spawn.category == arv::world::SpawnCategory::Passive;
      });
  ASSERT_NE(horse, plains->second.spawns.end());
  EXPECT_EQ(horse->weight, 2U);
  EXPECT_EQ(horse->min_group, 1U);
  EXPECT_EQ(horse->max_group, 1U);
  EXPECT_TRUE(horse->conditions.requires_surface);

  const arv::voxel::BlockRegistry blocks =
      load_first_party_block_registry(packs.value());
  const std::optional<arv::voxel::BlockVariantID> grass =
      blocks.find_variant("arvane:grass_block");
  ASSERT_TRUE(grass.has_value());
  EXPECT_TRUE(blocks.has_tag(
      blocks.block_id(grass.value()),
      "arvane:spawn/grass_like"));

  for (std::string_view biome_id : {
           "arvane:worldgen/biome/plains",
           "arvane:worldgen/biome/forest",
           "arvane:worldgen/biome/rolling_hills",
           "arvane:worldgen/biome/scrubland",
           "arvane:worldgen/biome/savanna",
           "arvane:worldgen/biome/highlands",
           "arvane:worldgen/biome/mountains",
       }) {
    const auto biome = registry.value().biomes.find(std::string{biome_id});
    ASSERT_NE(biome, registry.value().biomes.end()) << biome_id;
    const auto& passive =
        biome->second.spawn_categories[
            arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)];
    EXPECT_TRUE(passive.specified) << biome_id;
    EXPECT_EQ(
        passive.budget_profile.id,
        "arvane:worldgen/spawn_budget_profile/passive_conservative") << biome_id;
    EXPECT_GT(passive.modifiers.density_multiplier, 0.0F) << biome_id;
    EXPECT_LE(passive.modifiers.density_multiplier, 1.0F) << biome_id;
    EXPECT_GT(passive.modifiers.soft_cap_multiplier, 0.0F) << biome_id;
    EXPECT_LE(passive.modifiers.soft_cap_multiplier, 1.0F) << biome_id;
    const auto found_pig = std::find_if(
        biome->second.spawns.begin(),
        biome->second.spawns.end(),
        [](const arv::world::worldgen::BiomeSpawnEntryConfig& spawn) {
          return spawn.id.id == "arvane:pig_grass" &&
                 spawn.entity.id == "arvane:pig" &&
                 spawn.category == arv::world::SpawnCategory::Passive;
        });
    EXPECT_NE(found_pig, biome->second.spawns.end()) << biome_id;
    const auto found_horse = std::find_if(
        biome->second.spawns.begin(),
        biome->second.spawns.end(),
        [](const arv::world::worldgen::BiomeSpawnEntryConfig& spawn) {
          return spawn.id.id == "arvane:horse_grass" &&
                 spawn.entity.id == "arvane:horse" &&
                 spawn.category == arv::world::SpawnCategory::Passive;
        });
    EXPECT_NE(found_horse, biome->second.spawns.end()) << biome_id;
  }

  const arv::entity::EntityRegistry entities =
      load_first_party_entity_registry(packs.value());
  auto dimension = arv::world::worldgen::resolve_dimension(
      registry.value(),
      "arvane:worldgen/dimension/overworld",
      blocks);
  ASSERT_TRUE(dimension) << dimension.error().describe();
  const arv::world::SpawnRuleRegistry rules =
      arv::world::SpawnRuleRegistry::build_from_dimension(
          *dimension.value(),
          entities,
          blocks);
  ASSERT_TRUE(rules.diagnostics().empty()) << rules.diagnostics().front().describe();
  const arv::world::BiomeSpawnTable* plains_table =
      rules.find_table("arvane:worldgen/biome/plains", arv::world::SpawnCategory::Passive);
  ASSERT_NE(plains_table, nullptr);
  EXPECT_TRUE(plains_table->has_budget);
  EXPECT_EQ(
      plains_table->budget.id,
      "arvane:worldgen/spawn_budget_profile/passive_conservative");
  EXPECT_FLOAT_EQ(plains_table->modifiers.density_multiplier, 1.0F);
  EXPECT_FLOAT_EQ(plains_table->modifiers.soft_cap_multiplier, 1.0F);
}

TEST(NaturalSpawnLoaderTests, RejectsInvalidSpawnCategoryWithDiagnostic) {
  arv::testsupport::ScopedTempDir temp{"arvane_spawn_loader_"};
  const std::filesystem::path packs_root = temp.path / "packs";
  write_pack_manifest(packs_root, "bad_pack", "bad");
  arv::testsupport::write_text(
      packs_root / "bad_pack" / "data" / "bad" / "worldgen" / "biome" / "bad.yaml",
      "id: bad:worldgen/biome/bad\n"
      "version: 1\n"
      "display_name: Bad\n"
      "spawns:\n"
      "  mystery:\n"
      "    - entity: bad:pig\n"
      "      weight: 1\n");

  auto packs = arv::asset::PackManager::load_from_packs_root(packs_root);
  ASSERT_TRUE(packs) << packs.error().describe();
  auto registry = arv::world::worldgen::load_worldgen_registry_from_packs(packs.value());
  ASSERT_FALSE(registry);
  EXPECT_NE(registry.error().describe().find("unknown spawn category"), std::string::npos)
      << registry.error().describe();
}

TEST(NaturalSpawnLoaderTests, RejectsOldInlineBiomeSpawnListFormat) {
  auto registry = load_spawn_loader_fixture(
      "",
      "",
      "  passive:\n"
      "    - entity: bad:pig\n"
      "      weight: 1\n");

  ASSERT_FALSE(registry);
  EXPECT_NE(registry.error().describe().find("must be an object"), std::string::npos)
      << registry.error().describe();
}

TEST(NaturalSpawnLoaderTests, RejectsMissingSpawnTableReference) {
  auto registry = load_spawn_loader_fixture(
      "",
      "",
      "  passive:\n"
      "    budget_profile: bad:worldgen/spawn_budget_profile/passive\n"
      "    density_multiplier: 1.0\n"
      "    soft_cap_multiplier: 1.0\n"
      "    use:\n"
      "      - table: bad:worldgen/spawn_table/missing\n"
      "    remove: []\n"
      "    add: []\n");

  ASSERT_FALSE(registry);
  EXPECT_NE(registry.error().describe().find("unknown spawn table"), std::string::npos)
      << registry.error().describe();
}

TEST(NaturalSpawnLoaderTests, RejectsMissingSpawnConditionReference) {
  auto registry = load_spawn_loader_fixture(
      "",
      "category: passive\n"
      "entries:\n"
      "  - id: bad:pig_grass\n"
      "    entity: bad:pig\n"
      "    weight: 1\n"
      "    conditions: bad:worldgen/spawn_condition/missing\n",
      "  passive:\n"
      "    budget_profile: bad:worldgen/spawn_budget_profile/passive\n"
      "    density_multiplier: 1.0\n"
      "    soft_cap_multiplier: 1.0\n"
      "    use:\n"
      "      - table: bad:worldgen/spawn_table/common\n"
      "    remove: []\n"
      "    add: []\n");

  ASSERT_FALSE(registry);
  EXPECT_NE(registry.error().describe().find("unknown spawn condition"), std::string::npos)
      << registry.error().describe();
}

TEST(NaturalSpawnLoaderTests, RejectsInvalidSpawnTableGroupRange) {
  auto registry = load_spawn_loader_fixture(
      "surface: true\n",
      "category: passive\n"
      "entries:\n"
      "  - id: bad:pig_grass\n"
      "    entity: bad:pig\n"
      "    weight: 1\n"
      "    min_group: 4\n"
      "    max_group: 2\n"
      "    conditions: bad:worldgen/spawn_condition/grass\n",
      "  passive:\n"
      "    budget_profile: bad:worldgen/spawn_budget_profile/passive\n"
      "    density_multiplier: 1.0\n"
      "    soft_cap_multiplier: 1.0\n"
      "    use:\n"
      "      - table: bad:worldgen/spawn_table/common\n"
      "    remove: []\n"
      "    add: []\n");

  ASSERT_FALSE(registry);
  EXPECT_NE(registry.error().describe().find("min_group must be <= max_group"), std::string::npos)
      << registry.error().describe();
}

TEST(NaturalSpawnLoaderTests, RejectsEmptyResolvedBiomeSpawnList) {
  auto registry = load_spawn_loader_fixture(
      "surface: true\n",
      "category: passive\n"
      "entries:\n"
      "  - id: bad:pig_grass\n"
      "    entity: bad:pig\n"
      "    weight: 1\n"
      "    conditions: bad:worldgen/spawn_condition/grass\n",
      "  passive:\n"
      "    budget_profile: bad:worldgen/spawn_budget_profile/passive\n"
      "    density_multiplier: 1.0\n"
      "    soft_cap_multiplier: 1.0\n"
      "    use:\n"
      "      - table: bad:worldgen/spawn_table/common\n"
      "    remove:\n"
      "      - bad:pig_grass\n"
      "    add: []\n");

  ASSERT_FALSE(registry);
  EXPECT_NE(registry.error().describe().find("must not be empty"), std::string::npos)
      << registry.error().describe();
}

TEST(NaturalSpawnRuleRegistryTests, SkipsInvalidEntityTypesAndTags) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  std::vector<arv::world::worldgen::BiomeSpawnEntryConfig> spawns;
  spawns.push_back(make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 5U));
  spawns.push_back(make_spawn_config("test:missing", arv::world::SpawnCategory::Passive, 5U));
  spawns.push_back(make_spawn_config("test:statue", arv::world::SpawnCategory::Passive, 5U));
  arv::world::worldgen::BiomeSpawnEntryConfig bad_tag =
      make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 5U);
  bad_tag.conditions.block_tags_below = {{"test:spawn/missing"}};
  spawns.push_back(std::move(bad_tag));
  const auto dimension = make_flat_spawn_dimension(std::move(spawns));

  const arv::world::SpawnRuleRegistry rules =
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks);

  EXPECT_EQ(rules.entry_count(), 1U);
  ASSERT_EQ(rules.diagnostics().size(), 3U);
  EXPECT_NE(rules.diagnostics()[0].describe().find("unknown entity type"), std::string::npos);
  EXPECT_NE(rules.diagnostics()[1].describe().find("not spawnable"), std::string::npos);
  EXPECT_NE(rules.diagnostics()[2].describe().find("unknown block tag"), std::string::npos);
  const arv::world::BiomeSpawnTable* table =
      rules.find_table(TestBiomeID, arv::world::SpawnCategory::Passive);
  ASSERT_NE(table, nullptr);
  EXPECT_EQ(table->total_weight, 5U);
}

TEST(NaturalSpawnRuleRegistryTests, WeightedPickWrapsRollsByTotalWeight) {
  arv::world::SpawnEntry first;
  first.entity_type_name = "test:first";
  first.weight = 1U;
  arv::world::SpawnEntry second;
  second.entity_type_name = "test:second";
  second.weight = 3U;
  arv::world::BiomeSpawnTable table;
  table.entries = {first, second};
  table.total_weight = 4U;

  ASSERT_NE(table.weighted_pick(0U), nullptr);
  EXPECT_EQ(table.weighted_pick(0U)->entity_type_name, "test:first");
  EXPECT_EQ(table.weighted_pick(1U)->entity_type_name, "test:second");
  EXPECT_EQ(table.weighted_pick(3U)->entity_type_name, "test:second");
  EXPECT_EQ(table.weighted_pick(4U)->entity_type_name, "test:first");
}

TEST(NaturalSpawnConditionTests, EnforcesLoadedActiveChunksSurfaceDistanceTagsAndLight) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::WorldRuntime world = make_spawn_world(blocks);
  const arv::world::SpawnEntry entry = make_spawn_entry(entities);
  arv::world::SpawnConditionEvaluator evaluator;
  arv::world::SpawnEvaluationContext context;
  context.block_registry = &blocks;
  context.player_positions.push_back({0.0F, 1.0F, 0.0F});
  const arv::BlockPos candidate{{1, 1, 1}};

  EXPECT_TRUE(evaluator.passes(
      world,
      entry,
      candidate,
      arv::world::SpawnCategory::Passive,
      context));

  const auto short_grass = blocks.find_variant("test:short_grass");
  ASSERT_TRUE(short_grass.has_value());
  ASSERT_TRUE(world.set_block(candidate, short_grass.value()));
  EXPECT_TRUE(evaluator.passes(
      world,
      entry,
      candidate,
      arv::world::SpawnCategory::Passive,
      context));
  ASSERT_TRUE(world.set_block(candidate, blocks.air_variant_id()));

  arv::world::SpawnEntry lit_entry = entry;
  lit_entry.conditions.requires_sky_access = true;
  lit_entry.conditions.min_light = 15U;
  EXPECT_FALSE(evaluator.passes(
      world,
      lit_entry,
      candidate,
      arv::world::SpawnCategory::Passive,
      context));
  set_body_light(world, candidate, arv::voxel::pack_light(0, 0, 0, 15));
  EXPECT_TRUE(evaluator.passes(
      world,
      lit_entry,
      candidate,
      arv::world::SpawnCategory::Passive,
      context));

  arv::world::SpawnEntry distant_entry = entry;
  distant_entry.conditions.min_distance_from_player_blocks = 16U;
  EXPECT_FALSE(evaluator.passes(
      world,
      distant_entry,
      candidate,
      arv::world::SpawnCategory::Passive,
      context));

  arv::world::SpawnEntry wrong_tag = entry;
  wrong_tag.conditions.block_tags_below = {"test:spawn/stone"};
  EXPECT_FALSE(evaluator.passes(
      world,
      wrong_tag,
      candidate,
      arv::world::SpawnCategory::Passive,
      context));

  arv::world::WorldRuntime loaded_only = make_spawn_world(blocks);
  loaded_only.set_chunk_lifecycle_state({{0, 0, 0}}, arv::voxel::ChunkLifecycleState::Loaded);
  EXPECT_FALSE(evaluator.passes(
      loaded_only,
      entry,
      candidate,
      arv::world::SpawnCategory::Passive,
      context));

  EXPECT_FALSE(evaluator.passes(
      world,
      entry,
      {{128, 1, 128}},
      arv::world::SpawnCategory::Passive,
      context));
}

TEST(NaturalSpawnLoadTests, EnforcesWorldAndLocalNaturalCaps) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::WorldRuntime world = make_spawn_world(blocks);
  const arv::world::SpawnEntry entry = make_spawn_entry(entities);

  arv::world::EntityCreateInfo info;
  info.type_id = entry.entity_type;
  info.type_name = entry.entity_type_name;
  info.position = {1.5F, 1.0F, 1.5F};
  const arv::EntityID entity = world.create_entity(std::move(info));
  ASSERT_TRUE(world.set_entity_natural_spawn(
      entity,
      {arv::world::SpawnCategory::Passive, entry.entity_type, entry.id, 0U, true}));

  arv::world::NaturalSpawningConfig config;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].per_player = 1U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].per_world_soft = 10U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].local_max = 1U;
  config.local_density_radius_blocks = 16U;

  arv::world::SpawnLoadTracker tracker;
  tracker.refresh(world);
  EXPECT_FALSE(tracker.has_world_budget(arv::world::SpawnCategory::Passive, 1U, config));
  EXPECT_FALSE(tracker.has_local_budget(
      world,
      {{2, 1, 2}},
      arv::world::SpawnCategory::Passive,
      entry.entity_type,
      config));
  EXPECT_TRUE(tracker.has_local_budget(
      world,
      {{80, 1, 80}},
      arv::world::SpawnCategory::Passive,
      entry.entity_type,
      config));
}

TEST(NaturalSpawnSystemTests, SpawnsMarkedNaturalEntitiesInLoadedActiveTerrain) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U)});
  arv::world::WorldRuntime world = make_spawn_world(blocks, dimension);
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {0.0F, 1.0F, 0.0F};

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.min_distance_from_player_blocks = 0U;
  config.max_distance_from_player_blocks = 32U;
  config.attempts_per_player_per_category = 64U;
  config.max_successes_per_player_per_tick = 4U;
  config.max_successes_per_world_per_tick = 4U;
  config.local_density_radius_blocks = 4U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].local_max = 4U;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks));

  system.tick(world, blocks, entities, 1U);

  EXPECT_GT(system.counters().attempts_total, 0U);
  EXPECT_GT(system.counters().successes_total, 0U);
  arv::u32 natural_count = 0U;
  for (const arv::EntityID id : world.entity_ids()) {
    const arv::world::NaturalSpawnComponent* natural = world.find_entity_natural_spawn(id);
    if (natural == nullptr) {
      continue;
    }
    ++natural_count;
    EXPECT_EQ(natural->category, arv::world::SpawnCategory::Passive);
    EXPECT_EQ(natural->spawn_tick, 1U);
    EXPECT_TRUE(natural->transient);
  }
  EXPECT_EQ(natural_count, system.counters().successes_total);
}

TEST(NaturalSpawnSystemTests, RespectsGlobalNaturalEntityCap) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U)});
  arv::world::WorldRuntime world = make_spawn_world(blocks, dimension);
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {0.0F, 1.0F, 0.0F};

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.min_distance_from_player_blocks = 0U;
  config.max_distance_from_player_blocks = 32U;
  config.attempts_per_player_per_category = 64U;
  config.max_successes_per_player_per_tick = 4U;
  config.max_successes_per_world_per_tick = 4U;
  config.max_natural_entities = 1U;
  config.local_density_radius_blocks = 4U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].local_max = 4U;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks));

  system.tick(world, blocks, entities, 1U);

  arv::u32 natural_count = 0U;
  for (const arv::EntityID id : world.entity_ids()) {
    if (world.find_entity_natural_spawn(id) != nullptr) {
      ++natural_count;
    }
  }
  EXPECT_EQ(natural_count, 1U);
  EXPECT_EQ(system.counters().successes_total, 1U);
  EXPECT_GT(system.counters().rejected_world_cap, 0U);
}

TEST(NaturalSpawnSystemTests, BudgetDensityTargetIsClampedByHardCap) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::SpawnBudgetProfileConfig budget = make_test_budget_profile();
  budget.population.soft_cap_per_player = 1U;
  budget.population.hard_cap_per_player = 3U;
  budget.population.density_per_eligible_chunk = 10.0F;
  budget.population.max_per_chunk = 8U;
  budget.population.max_per_3x3_chunks = 32U;
  budget.max_successes_per_interval = 8U;
  budget.credit.refill_per_interval = 128.0F;
  budget.credit.max_bank = 128.0F;
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U)},
      budget);
  arv::world::WorldRuntime world = make_spawn_world(blocks, dimension);
  world.ensure_player(arv::PlayerID{1}).position = {0.0F, 1.0F, 0.0F};

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.max_successes_per_world_per_tick = 16U;
  config.max_natural_entities = 64U;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks));

  system.tick(world, blocks, entities, 1U);

  EXPECT_EQ(count_natural_entities(world), 3U);
  EXPECT_GT(system.counters().rejected_category_cap, 0U);
}

TEST(NaturalSpawnSystemTests, CreditsAccumulateWhenUnderBudgetAndStopAtTarget) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::SpawnBudgetProfileConfig budget = make_test_budget_profile();
  budget.population.soft_cap_per_player = 2U;
  budget.population.hard_cap_per_player = 4U;
  budget.population.density_per_eligible_chunk = 0.0F;
  budget.max_successes_per_interval = 1U;
  budget.credit.refill_per_interval = 2.0F;
  budget.credit.max_bank = 2.0F;
  budget.credit.spend_per_group_attempt = 1.0F;
  budget.credit.spend_per_entity_spawned = 1.0F;
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U)},
      budget);
  arv::world::WorldRuntime world = make_spawn_world(blocks, dimension);
  world.ensure_player(arv::PlayerID{1}).position = {0.0F, 1.0F, 0.0F};

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.max_successes_per_world_per_tick = 8U;
  config.max_natural_entities = 64U;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks));

  system.tick(world, blocks, entities, 1U);
  EXPECT_EQ(count_natural_entities(world), 1U);
  system.tick(world, blocks, entities, 2U);
  EXPECT_EQ(count_natural_entities(world), 2U);
  const arv::u64 attempts_after_target = system.counters().attempts_total;
  system.tick(world, blocks, entities, 3U);

  EXPECT_EQ(count_natural_entities(world), 2U);
  EXPECT_EQ(system.counters().attempts_total, attempts_after_target);
  EXPECT_GT(system.counters().rejected_category_cap, 0U);
}

TEST(NaturalSpawnSystemTests, EntryCapsLimitRareNaturalEntries) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::SpawnBudgetProfileConfig budget = make_test_budget_profile();
  budget.population.soft_cap_per_player = 8U;
  budget.population.hard_cap_per_player = 8U;
  budget.population.density_per_eligible_chunk = 0.0F;
  budget.max_successes_per_interval = 8U;
  budget.credit.refill_per_interval = 128.0F;
  budget.credit.max_bank = 128.0F;
  auto spawn = make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U);
  spawn.caps.max_near_player = 1U;
  const auto dimension = make_flat_spawn_dimension({spawn}, budget);
  arv::world::WorldRuntime world = make_spawn_world(blocks, dimension);
  world.ensure_player(arv::PlayerID{1}).position = {0.0F, 1.0F, 0.0F};

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.max_successes_per_world_per_tick = 16U;
  config.max_natural_entities = 64U;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks));

  system.tick(world, blocks, entities, 1U);

  EXPECT_EQ(count_natural_entities(world), 1U);
  EXPECT_GT(system.counters().rejected_entry_cap, 0U);
}

TEST(NaturalSpawnSystemTests, DeterministicBudgetSimulationReportsCounters) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::SpawnBudgetProfileConfig budget = make_test_budget_profile();
  budget.population.soft_cap_per_player = 6U;
  budget.population.hard_cap_per_player = 10U;
  budget.population.density_per_eligible_chunk = 0.25F;
  budget.max_successes_per_interval = 2U;
  budget.credit.refill_per_interval = 8.0F;
  budget.credit.max_bank = 16.0F;
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U)},
      budget);
  arv::world::WorldRuntime world = make_spawn_world(blocks, dimension);
  world.ensure_player(arv::PlayerID{1}).position = {0.0F, 1.0F, 0.0F};
  world.ensure_player(arv::PlayerID{2}).position = {48.0F, 1.0F, 0.0F};

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.max_successes_per_world_per_tick = 16U;
  config.max_natural_entities = 64U;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks));

  const auto started = std::chrono::steady_clock::now();
  for (arv::TickIndex tick = 1U; tick <= 12U; ++tick) {
    system.tick(world, blocks, entities, tick);
  }
  const auto elapsed_us =
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - started)
          .count();

  const arv::world::NaturalSpawnCounters& counters = system.counters();
  RecordProperty("attempts", static_cast<long long>(counters.attempts_total));
  RecordProperty("successes", static_cast<long long>(counters.successes_total));
  RecordProperty("final_natural_count", count_natural_entities(world));
  RecordProperty("cap_rejections", static_cast<long long>(
      counters.rejected_category_cap +
      counters.rejected_world_cap +
      counters.rejected_local_density +
      counters.rejected_entry_cap));
  RecordProperty("condition_rejections", static_cast<long long>(counters.rejected_conditions));
  RecordProperty("elapsed_us", static_cast<long long>(elapsed_us));

  EXPECT_GT(counters.attempts_total, 0U);
  EXPECT_GT(counters.successes_total, 0U);
  EXPECT_LE(count_natural_entities(world), budget.population.hard_cap_per_player * 2U);
}

TEST(NaturalSpawnJobTests, BuildsSpawnAndDespawnPlansWithoutMutatingWorld) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U)});
  arv::world::WorldRuntime world = make_spawn_world(blocks, dimension);
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {0.0F, 1.0F, 0.0F};
  const arv::world::SpawnEntry entry = make_spawn_entry(entities);

  arv::world::EntityCreateInfo info;
  info.type_id = entry.entity_type;
  info.type_name = entry.entity_type_name;
  info.position = {200.0F, 1.0F, 200.0F};
  const arv::EntityID transient_hostile = world.create_entity(std::move(info));
  ASSERT_TRUE(world.set_entity_natural_spawn(
      transient_hostile,
      {arv::world::SpawnCategory::Hostile, entry.entity_type, entry.id, 0U, true}));
  const arv::usize entity_count_before = world.entity_count();

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.min_distance_from_player_blocks = 0U;
  config.max_distance_from_player_blocks = 32U;
  config.attempts_per_player_per_category = 64U;
  config.max_successes_per_player_per_tick = 4U;
  config.max_successes_per_world_per_tick = 4U;
  config.local_density_radius_blocks = 4U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].local_max = 4U;
  config.despawning[arv::world::spawn_category_index(arv::world::SpawnCategory::Hostile)].min_lifetime_ticks = 0U;
  config.despawning[arv::world::spawn_category_index(arv::world::SpawnCategory::Hostile)].soft_distance_blocks = 8U;
  config.despawning[arv::world::spawn_category_index(arv::world::SpawnCategory::Hostile)].hard_distance_blocks = 16U;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks));

  std::optional<arv::world::NaturalSpawnJobRequest> request =
      system.make_job_request(world, 1U);
  ASSERT_TRUE(request.has_value());
  arv::world::NaturalSpawnJobResult result =
      arv::world::NaturalSpawnSystem::build_job_result(std::move(request.value()));

  EXPECT_GT(result.counters.attempts_total, 0U);
  EXPECT_FALSE(result.spawn_intents.empty());
  EXPECT_GT(result.counters.spawn_intents_generated, 0U);
  EXPECT_FALSE(result.despawn_intents.empty());
  EXPECT_GT(result.counters.despawn_intents_generated, 0U);
  EXPECT_EQ(world.entity_count(), entity_count_before);
  EXPECT_TRUE(world.entity_exists(transient_hostile));
}

TEST(NaturalSpawnJobTests, ProfileTickIntervalGatesJobRequests) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::SpawnBudgetProfileConfig budget = make_test_budget_profile();
  budget.tick_interval = 20U;
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U)},
      budget);
  arv::world::WorldRuntime world = make_spawn_world(blocks, dimension);
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {0.0F, 1.0F, 0.0F};

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks));

  EXPECT_FALSE(system.make_job_request(world, 19U).has_value());
  std::optional<arv::world::NaturalSpawnJobRequest> request =
      system.make_job_request(world, 20U);
  ASSERT_TRUE(request.has_value());
  EXPECT_EQ(request->tick, 20U);
}

TEST(NaturalSpawnJobTests, SpawnsOnLoadedGroundWhenPlayerIsFlyingAboveSurface) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U)});
  arv::world::WorldRuntime world = make_spawn_world(blocks, dimension);
  for (const arv::ChunkSectionCoord coord : world.loaded_chunk_coords()) {
    ASSERT_TRUE(world.set_chunk_lifecycle_state(
        coord,
        arv::voxel::ChunkLifecycleState::Loaded));
  }
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {0.0F, 64.0F, 0.0F};

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.min_distance_from_player_blocks = 0U;
  config.max_distance_from_player_blocks = 32U;
  config.attempts_per_player_per_category = 64U;
  config.max_successes_per_player_per_tick = 4U;
  config.max_successes_per_world_per_tick = 4U;
  config.local_density_radius_blocks = 4U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].local_max = 4U;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks));

  system.tick(world, blocks, entities, 1U);

  EXPECT_GT(system.counters().successes_total, 0U);
  EXPECT_EQ(system.counters().rejected_chunk_not_simulated, 0U);
}

TEST(NaturalSpawnSystemTests, PigsCanSpawnNearCurrentIntegratedSinglePlayerSeed) {
  auto packs = arv::asset::PackManager::load_from_packs_root("arvane_home/packs");
  ASSERT_TRUE(packs) << packs.error().describe();
  const arv::voxel::BlockRegistry blocks =
      load_first_party_block_registry(packs.value());
  const arv::entity::EntityRegistry entities =
      load_first_party_entity_registry(packs.value());
  const std::shared_ptr<const arv::world::worldgen::ResolvedDimension> dimension =
      load_first_party_overworld_dimension(packs.value(), blocks);

  const arv::world::SpawnRuleRegistry rules =
      arv::world::SpawnRuleRegistry::build_from_dimension(
          *dimension,
          entities,
          blocks);
  ASSERT_GT(rules.entry_count(), 0U);
  if (!rules.diagnostics().empty()) {
    ADD_FAILURE() << rules.diagnostics().front().describe();
  }
  ASSERT_TRUE(rules.diagnostics().empty());

  constexpr arv::u64 CurrentSinglePlayerSeed = 32187121430964ULL;
  struct SavedPositionCase {
    const char* name;
    arv::Vec3f position;
    arv::TickIndex tick;
  };
  const std::array<SavedPositionCase, 3> saved_positions{{
      {"older_regression_position",
       {495.6519775390625F, 46.74935531616211F, 213.01556396484375F},
       136000U},
      {"current_Hippah_position",
       {161.0006866455078F, 46.000999450683594F, 79.33663940429688F},
       136200U},
      {"current_Hippo_position",
       {141.09474182128906F, 43.000999450683594F, -7.985311031341553F},
       136400U},
  }};

  for (const SavedPositionCase& spawn_case : saved_positions) {
    SCOPED_TRACE(spawn_case.name);
    const arv::ChunkCoord center{
        arv::floor_div(
            static_cast<arv::i32>(std::floor(spawn_case.position.x)),
            arv::ChunkSize),
        arv::floor_div(
            static_cast<arv::i32>(std::floor(spawn_case.position.z)),
            arv::ChunkSize)};

    arv::world::WorldRuntime world{arv::WorldID{1}};
    world.set_day_time_tick(44384U);
    publish_generated_columns(world, dimension, CurrentSinglePlayerSeed, center, 5);
    arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
    player.position = spawn_case.position;

    arv::world::NaturalSpawnSystem system;
    arv::world::NaturalSpawningConfig config;
    config.interval_ticks = 1U;
    config.min_distance_from_player_blocks = 24U;
    config.max_distance_from_player_blocks = 64U;
    config.attempts_per_player_per_category = 512U;
    config.max_successes_per_player_per_tick = 8U;
    config.max_successes_per_world_per_tick = 8U;
    config.local_density_radius_blocks = 32U;
    config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)]
        .per_player = 64U;
    config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)]
        .per_world_soft = 256U;
    config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)]
        .local_max = 32U;
    system.configure(config);
    system.set_rules(rules);

    arv::u32 pig_count = 0U;
    for (arv::u32 interval = 0U; interval < 10U && pig_count == 0U; ++interval) {
      system.tick(world, blocks, entities, spawn_case.tick + interval * 200U);
      for (const arv::EntityID id : world.entity_ids()) {
        const arv::world::NaturalSpawnComponent* natural =
            world.find_entity_natural_spawn(id);
        const arv::entity::EntityType* type = world.find_entity_type(id);
        if (natural != nullptr && type != nullptr && type->type_name == "arvane:pig") {
          ++pig_count;
        }
      }
    }
    const arv::world::NaturalSpawnCounters& counters = system.counters();
    EXPECT_GT(pig_count, 0U)
        << "attempts=" << counters.attempts_total
        << " intents=" << counters.spawn_intents_generated
        << " committed=" << counters.spawn_intents_committed
        << " no_table=" << counters.rejected_no_biome_table
        << " conditions=" << counters.rejected_conditions
        << " local_density=" << counters.rejected_local_density
        << " group_failed=" << counters.rejected_group_failed
        << " not_loaded=" << counters.rejected_chunk_not_loaded
        << " not_simulated=" << counters.rejected_chunk_not_simulated;
  }
}

TEST(NaturalSpawnJobTests, ApplyRevalidatesBeforeCommittingSpawnIntents) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::WorldRuntime world = make_spawn_world(blocks);
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {0.0F, 1.0F, 0.0F};
  const arv::world::SpawnEntry entry = make_spawn_entry(entities);

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.min_distance_from_player_blocks = 0U;
  config.max_distance_from_player_blocks = 32U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].local_max = 4U;
  system.configure(config);

  arv::world::NaturalSpawnJobResult stale;
  stale.world_id = world.id();
  stale.source_tick = 1U;
  stale.spawn_intents.push_back({entry, make_test_budget_profile(), {{{1, 1, 1}}}});
  const arv::world::NaturalSpawnApplyStats stale_stats =
      system.apply_job_result(world, blocks, entities, stale, 8U);
  EXPECT_TRUE(stale_stats.stale);
  EXPECT_EQ(stale_stats.spawned, 0U);

  arv::world::NaturalSpawnJobResult blocked;
  blocked.world_id = world.id();
  blocked.source_tick = 8U;
  blocked.spawn_intents.push_back({entry, make_test_budget_profile(), {{{1, 1, 1}}}});
  const auto grass = blocks.find_variant("test:grass_block");
  ASSERT_TRUE(grass.has_value());
  ASSERT_TRUE(world.set_block({{1, 1, 1}}, grass.value()));
  const arv::world::NaturalSpawnApplyStats blocked_stats =
      system.apply_job_result(world, blocks, entities, blocked, 8U);
  EXPECT_EQ(blocked_stats.spawned, 0U);

  arv::world::NaturalSpawnJobResult valid;
  valid.world_id = world.id();
  valid.source_tick = 8U;
  valid.spawn_intents.push_back({entry, make_test_budget_profile(), {{{2, 1, 2}}}});
  const arv::world::NaturalSpawnApplyStats valid_stats =
      system.apply_job_result(world, blocks, entities, valid, 8U);
  EXPECT_EQ(valid_stats.spawned, 1U);
  EXPECT_EQ(system.counters().spawn_intents_committed, 1U);
}

TEST(NaturalSpawnJobTests, ApplyCommitsValidSubsetOfRevalidatedGroup) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::WorldRuntime world = make_spawn_world(blocks);
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {0.0F, 1.0F, 0.0F};
  const arv::world::SpawnEntry entry = make_spawn_entry(entities);

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.min_distance_from_player_blocks = 0U;
  config.max_distance_from_player_blocks = 32U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)]
      .local_max = 4U;
  system.configure(config);

  const auto grass = blocks.find_variant("test:grass_block");
  ASSERT_TRUE(grass.has_value());
  ASSERT_TRUE(world.set_block({{1, 1, 1}}, grass.value()));

  arv::world::NaturalSpawnJobResult mixed;
  mixed.world_id = world.id();
  mixed.source_tick = 8U;
  mixed.spawn_intents.push_back(
      {entry, make_test_budget_profile(), {{{2, 1, 2}, {1, 1, 1}}}});
  const arv::world::NaturalSpawnApplyStats stats =
      system.apply_job_result(world, blocks, entities, mixed, 8U);

  EXPECT_EQ(stats.spawned, 1U);
  EXPECT_EQ(system.counters().spawn_intents_committed, 1U);
  EXPECT_EQ(system.counters().rejected_group_failed, 0U);
}

TEST(NaturalSpawnJobTests, ProfileTickIntervalKeepsAsyncResultsFresh) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::SpawnBudgetProfileConfig budget = make_test_budget_profile();
  budget.tick_interval = 200U;
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U)},
      budget);
  arv::world::WorldRuntime world = make_spawn_world(blocks, dimension);
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {0.0F, 1.0F, 0.0F};
  const arv::world::SpawnEntry entry = make_spawn_entry(entities);

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 10U;
  config.min_distance_from_player_blocks = 0U;
  config.max_distance_from_player_blocks = 32U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)]
      .local_max = 4U;
  system.configure(config);
  system.set_rules(
      arv::world::SpawnRuleRegistry::build_from_dimension(*dimension, entities, blocks));

  arv::world::NaturalSpawnJobResult delayed;
  delayed.world_id = world.id();
  delayed.source_tick = 200U;
  delayed.spawn_intents.push_back({entry, budget, {{{2, 1, 2}}}});
  const arv::world::NaturalSpawnApplyStats stats =
      system.apply_job_result(world, blocks, entities, delayed, 230U);

  EXPECT_FALSE(stats.stale);
  EXPECT_EQ(stats.spawned, 1U);
  EXPECT_EQ(system.counters().spawn_intents_committed, 1U);
}

TEST(NaturalSpawnJobTests, ApplyVehicleSpawnIntentUsesVehicleDefinitions) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_vehicle_entity_registry();
  const arv::entity::VehicleDefinitionRegistry vehicles = make_spawn_vehicle_registry();
  arv::world::WorldRuntime world = make_spawn_world(blocks);
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {0.0F, 1.0F, 0.0F};
  const arv::world::SpawnEntry entry = make_spawn_vehicle_entry(entities);

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.min_distance_from_player_blocks = 0U;
  config.max_distance_from_player_blocks = 32U;
  config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].local_max = 4U;
  system.configure(config);

  arv::world::NaturalSpawnJobResult valid;
  valid.world_id = world.id();
  valid.source_tick = 8U;
  valid.spawn_intents.push_back({entry, make_test_budget_profile(), {{{2, 1, 2}}}});
  const arv::world::NaturalSpawnApplyStats stats =
      system.apply_job_result(world, blocks, entities, valid, 8U, false, &vehicles);

  ASSERT_EQ(stats.spawned, 1U);
  arv::EntityID spawned{};
  for (const arv::EntityID id : world.entity_ids()) {
    const arv::entity::EntityType* type = world.find_entity_type(id);
    if (type != nullptr && type->type_name == "test:horse") {
      spawned = id;
      break;
    }
  }
  ASSERT_NE(spawned, (arv::EntityID{}));
  EXPECT_NE(world.find_entity_rideable(spawned), nullptr);
  EXPECT_NE(world.find_entity_vehicle_physics(spawned), nullptr);
}

TEST(NaturalSpawnServerTests, RejectsInvalidSpawnEntriesBeforeRuntimeUse) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  auto items_result =
      arv::player::ItemRegistry::build_from_blocks_and_items(blocks, {});
  ASSERT_TRUE(items_result);
  const arv::player::ItemRegistry items = std::move(items_result.value());
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:missing", arv::world::SpawnCategory::Passive, 20U)});

  arv::world::WorldGenerationConfig generation_config =
      arv::world::config_from_resolved_dimension(dimension);
  auto palette = arv::world::resolve_world_generation_palette(generation_config, blocks);
  ASSERT_TRUE(palette) << palette.error().message;
  arv::server::ServerChunkGenerationSettings generation;
  generation.enabled = true;
  generation.seed = 42U;
  generation.config = generation_config;
  generation.palette = std::move(palette.value());

  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &blocks;
  start_info.item_registry = &items;
  start_info.entity_registry = &entities;
  start_info.chunk_generation = std::move(generation);
  start_info.natural_spawning.enabled = true;

  const arv::net::NetworkResult started = server.start(start_info);
  ASSERT_FALSE(started);
  EXPECT_NE(started.error().message.find("invalid spawn entries"), std::string::npos)
      << started.error().message;
  EXPECT_NE(started.error().message.find("unknown entity type"), std::string::npos)
      << started.error().message;
}

TEST(NaturalSpawnServerTests, RunsNaturalSpawningAsAsyncWorldJob) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  auto items_result =
      arv::player::ItemRegistry::build_from_blocks_and_items(blocks, {});
  ASSERT_TRUE(items_result);
  const arv::player::ItemRegistry items = std::move(items_result.value());
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  const auto dimension = make_flat_spawn_dimension(
      {make_spawn_config("test:pig", arv::world::SpawnCategory::Passive, 20U)});

  arv::world::WorldGenerationConfig generation_config =
      arv::world::config_from_resolved_dimension(dimension);
  auto palette = arv::world::resolve_world_generation_palette(generation_config, blocks);
  ASSERT_TRUE(palette) << palette.error().message;
  arv::server::ServerChunkGenerationSettings generation;
  generation.enabled = true;
  generation.seed = 42U;
  generation.config = generation_config;
  generation.palette = std::move(palette.value());

  arv::world::NaturalSpawningConfig spawn_config;
  spawn_config.interval_ticks = 1U;
  spawn_config.min_distance_from_player_blocks = 0U;
  spawn_config.max_distance_from_player_blocks = 32U;
  spawn_config.attempts_per_player_per_category = 64U;
  spawn_config.max_successes_per_player_per_tick = 2U;
  spawn_config.max_successes_per_world_per_tick = 2U;
  spawn_config.local_density_radius_blocks = 4U;
  spawn_config.caps[arv::world::spawn_category_index(arv::world::SpawnCategory::Passive)].local_max = 8U;

  auto transports = arv::net::make_in_process_transport_pair();
  arv::server::ServerRuntime server(std::move(transports.server));
  arv::server::ServerStartInfo start_info;
  start_info.mode = arv::server::ServerMode::Integrated;
  start_info.listen_address = {"in-process", 0};
  start_info.block_registry = &blocks;
  start_info.item_registry = &items;
  start_info.entity_registry = &entities;
  start_info.chunk_generation = std::move(generation);
  start_info.natural_spawning = spawn_config;
  start_info.job_system.max_ai_jobs_in_flight = 1U;
  ASSERT_TRUE(server.start(start_info));

  arv::client::ClientRuntime client(std::move(transports.client));
  ASSERT_TRUE(client.connect_integrated());
  server.tick();
  client.fixed_tick(server.tick_index());
  ASSERT_EQ(client.connection_state(), arv::net::ConnectionState::InGame);
  ASSERT_EQ(server.sessions().size(), 1U);
  const arv::server::ServerSession* session = server.sessions().first();
  ASSERT_NE(session, nullptr);

  fill_spawn_chunks(server.voxel_world(), blocks);
  arv::player::PlayerState* connected_player =
      server.voxel_world().find_player(session->player_id);
  ASSERT_NE(connected_player, nullptr);
  arv::player::PlayerState& player = *connected_player;
  player.position = {0.0F, 1.0F, 0.0F};

  for (int tick = 0; tick < 80; ++tick) {
    server.tick();
    client.fixed_tick(server.tick_index());
    const arv::world::NaturalSpawnCounters& counters =
        server.natural_spawn_counters();
    if (counters.spawn_jobs_completed > 0U &&
        counters.spawn_intents_committed > 0U) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const arv::world::NaturalSpawnCounters& counters =
      server.natural_spawn_counters();
  EXPECT_GT(counters.spawn_jobs_submitted, 0U);
  EXPECT_GT(counters.spawn_jobs_completed, 0U);
  EXPECT_GT(counters.spawn_intents_generated, 0U);
  EXPECT_GT(counters.spawn_intents_committed, 0U);
  EXPECT_LE(
      counters.successes_last_interval,
      spawn_config.max_successes_per_world_per_tick);
  server.shutdown();
}

TEST(NaturalSpawnSystemTests, DespawnsTransientNaturalEntitiesButKeepsOthers) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::WorldRuntime world = make_spawn_world(blocks);
  arv::player::PlayerState& player = world.ensure_player(arv::PlayerID{1});
  player.position = {0.0F, 1.0F, 0.0F};
  const arv::world::SpawnEntry entry = make_spawn_entry(entities);

  auto create_entity_at = [&](arv::Vec3f position, bool persistent) {
    arv::world::EntityCreateInfo info;
    info.type_id = entry.entity_type;
    info.type_name = entry.entity_type_name;
    info.position = position;
    info.persistent = persistent;
    return world.create_entity(std::move(info));
  };

  const arv::EntityID transient_hostile = create_entity_at({200.0F, 1.0F, 200.0F}, false);
  ASSERT_TRUE(world.set_entity_natural_spawn(
      transient_hostile,
      {arv::world::SpawnCategory::Hostile, entry.entity_type, entry.id, 0U, true}));
  const arv::EntityID persistent_hostile = create_entity_at({220.0F, 1.0F, 200.0F}, true);
  ASSERT_TRUE(world.set_entity_natural_spawn(
      persistent_hostile,
      {arv::world::SpawnCategory::Hostile, entry.entity_type, entry.id, 0U, false}));
  const arv::EntityID passive = create_entity_at({240.0F, 1.0F, 200.0F}, true);
  ASSERT_TRUE(world.set_entity_natural_spawn(
      passive,
      {arv::world::SpawnCategory::Passive, entry.entity_type, entry.id, 0U, false}));
  const arv::EntityID ordinary = create_entity_at({260.0F, 1.0F, 200.0F}, false);

  arv::world::NaturalSpawnSystem system;
  arv::world::NaturalSpawningConfig config;
  config.interval_ticks = 1U;
  config.enabled = true;
  config.attempts_per_player_per_category = 0U;
  config.despawning[arv::world::spawn_category_index(arv::world::SpawnCategory::Hostile)].min_lifetime_ticks = 0U;
  config.despawning[arv::world::spawn_category_index(arv::world::SpawnCategory::Hostile)].soft_distance_blocks = 8U;
  config.despawning[arv::world::spawn_category_index(arv::world::SpawnCategory::Hostile)].hard_distance_blocks = 16U;
  system.configure(config);

  system.tick(world, blocks, entities, 20U);

  EXPECT_FALSE(world.entity_exists(transient_hostile));
  EXPECT_TRUE(world.entity_exists(persistent_hostile));
  EXPECT_TRUE(world.entity_exists(passive));
  EXPECT_TRUE(world.entity_exists(ordinary));
  EXPECT_EQ(system.counters().despawned_natural_hostile, 1U);
}

TEST(NaturalSpawnPersistenceTests, FiltersTransientNaturalEntitiesFromSaveRecords) {
  const arv::voxel::BlockRegistry blocks = make_spawn_block_registry();
  const arv::entity::EntityRegistry entities = make_spawn_entity_registry();
  arv::world::WorldRuntime world = make_spawn_world(blocks);
  const arv::world::SpawnEntry entry = make_spawn_entry(entities);

  auto create_persistent = [&](std::string_view name, bool transient) {
    arv::world::EntityCreateInfo info;
    info.type_id = entry.entity_type;
    info.type_name = std::string{name};
    info.position = {1.0F, 1.0F, 1.0F};
    info.persistent = true;
    const arv::EntityID entity = world.create_entity(std::move(info));
    EXPECT_TRUE(world.set_entity_natural_spawn(
        entity,
        {arv::world::SpawnCategory::Passive, entry.entity_type, entry.id, 0U, transient}));
    return entity;
  };

  (void)create_persistent("test:transient_pig", true);
  (void)create_persistent("test:persistent_pig", false);

  const std::vector<arv::entity::PersistentEntityRecord> records =
      world.persistent_entity_records();
  ASSERT_EQ(records.size(), 1U);
  EXPECT_EQ(records.front().type_name, "test:persistent_pig");
}
