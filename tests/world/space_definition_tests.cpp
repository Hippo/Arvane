#include "arv/asset/pack.hpp"
#include "arv/world/space_definition.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace {

using arv::testsupport::ScopedTempDir;
using arv::testsupport::write_text;

void write_pack_manifest(
    const std::filesystem::path& packs_root,
    std::string_view pack_id,
    std::string_view name_space) {
  write_text(
      packs_root / std::string{pack_id} / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" +
          std::string{pack_id} + "\"\n"
                                 "display_name: \"" +
          std::string{pack_id} + "\"\n"
                                 "version: \"1.0.0\"\n"
                                 "namespaces:\n"
                                 "  - \"" +
          std::string{name_space} + "\"\n"
                                    "side: \"both\"\n"
                                    "dependencies: []\n"
                                    "load_after: []\n"
                                    "load_before: []\n"
                                    "capabilities:\n"
                                    "  assets: false\n"
                                    "  data: true\n"
                                    "  config: false\n"
                                    "  native_plugin: false\n");
}

void write_valid_space_pack(const std::filesystem::path& packs_root) {
  write_pack_manifest(packs_root, "space_test", "test");
  const std::filesystem::path root = packs_root / "space_test" / "data" / "test";

  write_text(
      root / "environment" / "planet.yaml",
      "id: test:environment/planet\n"
      "version: 1\n"
      "gravity_scale: 1.0\n"
      "has_atmosphere: true\n"
      "has_oxygen: true\n");
  write_text(
      root / "environment" / "space.yaml",
      "id: test:environment/space\n"
      "version: 1\n"
      "gravity_scale: 0.0\n"
      "has_atmosphere: false\n"
      "has_oxygen: false\n");
  write_text(
      root / "world" / "planet.yaml",
      "id: test:world/planet\n"
      "version: 1\n"
      "kind: planet\n"
      "display_name: Test Planet\n"
      "worldgen: test:worldgen/dimension/test\n"
      "environment: test:environment/planet\n"
      "space_exit:\n"
      "  enabled: true\n"
      "  target_space_world: test:space/system\n"
      "  min_y: 128\n"
      "  min_vertical_speed: 10.0\n"
      "  require_vehicle_tags:\n"
      "    - test:space_capable\n");
  write_text(
      root / "space" / "system.yaml",
      "id: test:space/system\n"
      "version: 1\n"
      "kind: space\n"
      "display_name: Test System\n"
      "environment: test:environment/space\n"
      "space:\n"
      "  coordinate_scale: 1.0\n"
      "  default_spawn_position: [0, 0, 0]\n"
      "  default_spawn_rotation: [0, 0, 0]\n"
      "  bodies:\n"
      "    - test:space_body/planet\n");
  write_text(
      root / "space_body" / "planet.yaml",
      "id: test:space_body/planet\n"
      "version: 1\n"
      "type: planet\n"
      "display_name: Test Planet\n"
      "space_world: test:space/system\n"
      "target_world: test:world/planet\n"
      "position: [100, 0, 0]\n"
      "visual_radius: 25.0\n"
      "entry_radius: 40.0\n"
      "render:\n"
      "  model: test:models/space_body/planet\n"
      "  texture: test:textures/space_body/planet\n"
      "  atmosphere_halo: true\n"
      "entry:\n"
      "  enabled: true\n"
      "  mode: fixed_spawn\n"
      "  require_vehicle_tags:\n"
      "    - test:space_capable\n"
      "  spawn_position: [0, 200, 0]\n"
      "  spawn_velocity: [0, -5, 0]\n"
      "  spawn_rotation: [0, 0, 0]\n");
}

} // namespace

TEST(SpaceDefinitionTests, LoadsBasePackSolarSystemDefinitions) {
  auto packs = arv::asset::PackManager::load_from_packs_root("arvane_home/packs");
  ASSERT_TRUE(packs) << packs.error().describe();

  auto registry = arv::world::load_space_definition_registry_from_packs(packs.value());
  ASSERT_TRUE(registry) << registry.error().describe();
  EXPECT_GE(registry.value().world_count(), 3U);
  EXPECT_GE(registry.value().environment_count(), 3U);
  EXPECT_GE(registry.value().space_body_count(), 3U);

  const arv::world::WorldDefinition* solar =
      registry.value().find_world("arvane:space/solar_system");
  ASSERT_NE(solar, nullptr);
  EXPECT_EQ(solar->kind, arv::world::WorldDefinitionKind::Space);
  ASSERT_TRUE(solar->space.has_value());
  EXPECT_EQ(solar->space->bodies.size(), 3U);

  const arv::world::WorldDefinition* luna =
      registry.value().find_world("arvane:world/luna");
  ASSERT_NE(luna, nullptr);
  EXPECT_EQ(luna->environment, "arvane:environment/luna");
  EXPECT_TRUE(luna->space_exit.has_value());
  EXPECT_FALSE(luna->space_exit->consume_fuel);
  EXPECT_LE(luna->space_exit->min_y, 128);

  const arv::world::SpaceBodyDefinition* luna_body =
      registry.value().find_space_body("arvane:space_body/luna");
  ASSERT_NE(luna_body, nullptr);
  EXPECT_EQ(luna_body->target_world, "arvane:world/luna");
  EXPECT_TRUE(luna_body->entry.enabled);
  ASSERT_TRUE(luna->space_exit.has_value());
  EXPECT_GT(
      arv::distance_squared(
          luna->space_exit->transition_spawn.position,
          luna_body->position),
      luna_body->entry_radius * luna_body->entry_radius);

  const arv::world::WorldDefinition* overworld =
      registry.value().find_world("arvane:world/overworld");
  ASSERT_NE(overworld, nullptr);
  const arv::world::SpaceBodyDefinition* overworld_body =
      registry.value().find_space_body("arvane:space_body/overworld");
  ASSERT_NE(overworld_body, nullptr);
  ASSERT_TRUE(overworld->space_exit.has_value());
  EXPECT_FALSE(overworld->space_exit->consume_fuel);
  EXPECT_LE(overworld->space_exit->min_y, 128);
  EXPECT_GT(
      arv::distance_squared(
          overworld->space_exit->transition_spawn.position,
          overworld_body->position),
      overworld_body->entry_radius * overworld_body->entry_radius);

  const arv::world::SpaceBodyDefinition* station =
      registry.value().find_space_body("arvane:space_body/research_station");
  ASSERT_NE(station, nullptr);
  EXPECT_EQ(station->type, arv::world::SpaceBodyType::Station);
  EXPECT_EQ(station->space_world, "arvane:space/solar_system");
  EXPECT_FALSE(station->entry.enabled);
  EXPECT_EQ(station->entry.mode, arv::world::SpaceBodyEntryMode::Disabled);
}

TEST(SpaceDefinitionTests, EmptyPackLoadsEmptyRegistry) {
  ScopedTempDir temp("arvane_space_defs_empty_");
  const std::filesystem::path packs_root = temp.path / "packs";
  write_pack_manifest(packs_root, "empty_space_test", "empty");

  auto packs = arv::asset::PackManager::load_from_packs_root(packs_root);
  ASSERT_TRUE(packs) << packs.error().describe();
  auto registry = arv::world::load_space_definition_registry_from_packs(packs.value());
  ASSERT_TRUE(registry) << registry.error().describe();
  EXPECT_EQ(registry.value().world_count(), 0U);
  EXPECT_EQ(registry.value().environment_count(), 0U);
  EXPECT_EQ(registry.value().space_body_count(), 0U);
}

TEST(SpaceDefinitionTests, RejectsEntryRadiusInsideVisualRadius) {
  ScopedTempDir temp("arvane_space_defs_radius_");
  const std::filesystem::path packs_root = temp.path / "packs";
  write_valid_space_pack(packs_root);
  write_text(
      packs_root / "space_test" / "data" / "test" / "space_body" / "planet.yaml",
      "id: test:space_body/planet\n"
      "version: 1\n"
      "type: planet\n"
      "display_name: Test Planet\n"
      "space_world: test:space/system\n"
      "target_world: test:world/planet\n"
      "position: [100, 0, 0]\n"
      "visual_radius: 50.0\n"
      "entry_radius: 40.0\n"
      "render:\n"
      "  model: test:models/space_body/planet\n"
      "  texture: test:textures/space_body/planet\n"
      "  atmosphere_halo: true\n"
      "entry:\n"
      "  enabled: true\n"
      "  mode: fixed_spawn\n"
      "  spawn_position: [0, 200, 0]\n");

  auto packs = arv::asset::PackManager::load_from_packs_root(packs_root);
  ASSERT_TRUE(packs) << packs.error().describe();
  auto registry = arv::world::load_space_definition_registry_from_packs(packs.value());
  ASSERT_FALSE(registry);
  EXPECT_NE(registry.error().describe().find("entry_radius"), std::string::npos);
}

TEST(SpaceDefinitionTests, RejectsUnknownBodyTargetWorld) {
  ScopedTempDir temp("arvane_space_defs_target_");
  const std::filesystem::path packs_root = temp.path / "packs";
  write_valid_space_pack(packs_root);
  write_text(
      packs_root / "space_test" / "data" / "test" / "space_body" / "planet.yaml",
      "id: test:space_body/planet\n"
      "version: 1\n"
      "type: planet\n"
      "display_name: Test Planet\n"
      "space_world: test:space/system\n"
      "target_world: test:world/missing\n"
      "position: [100, 0, 0]\n"
      "visual_radius: 25.0\n"
      "entry_radius: 40.0\n"
      "render:\n"
      "  model: test:models/space_body/planet\n"
      "  texture: test:textures/space_body/planet\n"
      "  atmosphere_halo: true\n"
      "entry:\n"
      "  enabled: true\n"
      "  mode: fixed_spawn\n"
      "  spawn_position: [0, 200, 0]\n");

  auto packs = arv::asset::PackManager::load_from_packs_root(packs_root);
  ASSERT_TRUE(packs) << packs.error().describe();
  auto registry = arv::world::load_space_definition_registry_from_packs(packs.value());
  ASSERT_FALSE(registry);
  EXPECT_NE(registry.error().describe().find("target_world"), std::string::npos);
  EXPECT_NE(registry.error().describe().find("test:world/missing"), std::string::npos);
}
