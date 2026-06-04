#include "arv/persistence/save.hpp"

#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

struct TempDir {
  std::filesystem::path path{};

  TempDir() {
    path = std::filesystem::temp_directory_path() /
           ("arvane_save_test_" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

arv::world::WorldGenerationConfig make_worldgen_config() {
  auto parsed = arv::world::parse_world_generation_yaml(R"yaml(
id: arvane:test_overworld
version: 1
seed_salt: "test"
vertical:
  min_y: 0
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

arv::persistence::SavedChunkSection make_saved_chunk(
    arv::ChunkSectionCoord coord,
    arv::voxel::BlockVariantID fill_variant) {
  arv::persistence::SavedChunkSection chunk;
  chunk.coord = coord;
  chunk.blocks.fill(fill_variant);
  chunk.lights.fill(arv::voxel::pack_light(0, 0, 0, 15));
  chunk.fluids.fill(arv::voxel::empty_fluid_cell());
  chunk.fluids[0] = arv::voxel::make_fluid_source(arv::voxel::make_water_fluid_definition());
  return chunk;
}

[[nodiscard]] arv::io::DataDocument stack_data(std::string_view value) {
  arv::io::DataDocument document{1U, arv::io::DataValue::object()};
  arv::io::DataValue payload = arv::io::DataValue::object();
  payload.set("value", std::string{value});
  document.root.set("test", std::move(payload));
  return document;
}

} // namespace

TEST(SaveTests, PlayerAndWorldSaveRoundTripThroughServerRoot) {
  TempDir temp_dir;
  const std::filesystem::path server_save_root =
      temp_dir.path / "arvane_home" / "saves" / "server-save-1";
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();
  const arv::player::ItemID stone_item =
      item_registry.find_item("arvane:stone").value();

  arv::persistence::PlayerSaveData player;
  player.profile_name = "local_player";
  player.world_name = "default";
  player.position = {1.0F, 2.0F, 3.0F};
  player.velocity = {0.1F, 0.2F, 0.3F};
  player.yaw_radians = 1.25F;
  player.pitch_radians = -0.5F;
  player.grounded = true;
  player.respawn_location =
      arv::Location{40.5F, 71.0F, -12.5F, 0.75F, -0.125F};
  player.game_mode_id = std::string{arv::player::CreativeGameModeID};
  player.permission_overrides.groups = {"admin", "builder"};
  player.permission_overrides.overrides.can_fly = arv::player::TriState::Allow;
  player.permission_overrides.custom_overrides["arvane.command.test"] =
      arv::player::TriState::Deny;
  player.selected_hotbar_slot = 3;
  ASSERT_TRUE(player.inventory.set_slot(
      0,
      arv::player::ItemStack{stone_item, 7, stack_data("player")}));

  const arv::persistence::SaveMetadata save_metadata{
      arv::persistence::CurrentSaveFormatVersion,
      std::string{arv::persistence::CurrentGameVersion},
      arv::persistence::hash_block_registry(block_registry),
      arv::persistence::hash_item_registry(item_registry),
      "Test Save"};
  arv::persistence::WorldMetadata world_metadata{
      "default",
      42,
      99,
      make_worldgen_config(),
      0,
      3,
      save_metadata.block_registry_hash,
      save_metadata.item_registry_hash};
  world_metadata.required_packs = {
      arv::persistence::RequiredPackMetadata{"arvane_base"},
      arv::persistence::RequiredPackMetadata{"test_pack"}};
  world_metadata.spawn_point =
      arv::Location{12.5F, 34.0F, -8.5F, 1.0F, -0.25F};
  const std::vector<arv::persistence::SavedChunkSection> chunks{
      make_saved_chunk({{0, 0, 0}}, stone)};
  const std::vector<arv::persistence::PlayerSaveData> players{player};
  const std::vector<arv::entity::PersistentEntityRecord> entities{
      {"entity-test-uuid-a",
       "arvane:test_entity",
       arv::WorldID{1},
       {4.0F, 5.0F, 6.0F},
       {0.0F, 0.0F, 0.0F, 1.0F},
       {0.1F, 0.2F, 0.3F}},
      {"entity-test-uuid-b",
       "arvane:test_entity",
       arv::WorldID{1},
       {-1.0F, 5.0F, -1.0F},
       {0.0F, 0.0F, 0.0F, 1.0F},
       {-0.1F, 0.0F, 0.1F}},
      {"dropped-item-uuid",
       std::string{arv::entity::DroppedItemEntityTypeName},
       arv::WorldID{1},
       {2.0F, 5.0F, 2.0F},
       {0.0F, 0.0F, 0.0F, 1.0F},
       {0.0F, 0.05F, 0.0F},
       arv::player::ItemStack{stone_item, 4, stack_data("drop")},
       0.25F}};

  ASSERT_TRUE(arv::persistence::save_server_root(
      server_save_root,
      save_metadata,
      world_metadata,
      chunks,
      players,
      entities,
      block_registry,
      item_registry));

  EXPECT_TRUE(std::filesystem::exists(server_save_root / "save.json"));
  EXPECT_TRUE(std::filesystem::exists(server_save_root / "permissions.yaml"));
  EXPECT_TRUE(std::filesystem::exists(server_save_root / "players" / "local_player.dat"));
  EXPECT_TRUE(std::filesystem::exists(server_save_root / "worlds" / "default" / "world.json"));
  EXPECT_FALSE(std::filesystem::exists(server_save_root / "worlds" / "default" / "entities.json"));
  EXPECT_TRUE(std::filesystem::exists(
      server_save_root / "worlds" / "default" / "entities" / "r.0.0.json"));
  EXPECT_TRUE(std::filesystem::exists(
      server_save_root / "worlds" / "default" / "entities" / "r.-1.-1.json"));
  {
    std::ifstream input(server_save_root / "players" / "local_player.dat");
    ASSERT_TRUE(input);
    const std::string player_json{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    EXPECT_NE(player_json.find("\"inventory_version\": 2"), std::string::npos);
    EXPECT_NE(player_json.find("\"respawn\""), std::string::npos);
    EXPECT_NE(player_json.find("\"data\""), std::string::npos);
    EXPECT_NE(player_json.find("\"player\""), std::string::npos);
  }
  {
    std::ifstream input(server_save_root / "worlds" / "default" / "world.json");
    ASSERT_TRUE(input);
    const std::string world_json{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    EXPECT_NE(world_json.find("\"arvane_base\""), std::string::npos);
    EXPECT_NE(world_json.find("\"test_pack\""), std::string::npos);
    EXPECT_NE(world_json.find("\"spawn\""), std::string::npos);
    EXPECT_NE(world_json.find("\"x\""), std::string::npos);
    EXPECT_EQ(world_json.find("\"position\""), std::string::npos);
    EXPECT_EQ(world_json.find("content_hash"), std::string::npos);
  }

  auto loaded = arv::persistence::load_server_root(
      server_save_root,
      block_registry,
      item_registry);

  ASSERT_TRUE(loaded);
  EXPECT_TRUE(loaded.value().has_save_metadata);
  EXPECT_TRUE(loaded.value().has_world_metadata);
  EXPECT_EQ(loaded.value().save.name, "Test Save");
  EXPECT_EQ(loaded.value().save.save_format_version, arv::persistence::CurrentSaveFormatVersion);
  EXPECT_EQ(loaded.value().world.seed, 42U);
  EXPECT_EQ(loaded.value().world.tick_index, 99U);
  EXPECT_EQ(loaded.value().world.min_chunk_y, 0);
  EXPECT_EQ(loaded.value().world.max_chunk_y, 4);
  ASSERT_EQ(loaded.value().world.required_packs.size(), 2U);
  EXPECT_EQ(loaded.value().world.required_packs[0].id, "arvane_base");
  EXPECT_EQ(loaded.value().world.required_packs[1].id, "test_pack");
  ASSERT_TRUE(loaded.value().world.spawn_point.has_value());
  EXPECT_EQ(
      arv::location_position(*loaded.value().world.spawn_point),
      (arv::Vec3f{12.5F, 34.0F, -8.5F}));
  EXPECT_FLOAT_EQ(loaded.value().world.spawn_point->yaw_radians, 1.0F);
  EXPECT_FLOAT_EQ(loaded.value().world.spawn_point->pitch_radians, -0.25F);
  ASSERT_EQ(loaded.value().chunks.size(), 1U);
  EXPECT_EQ(loaded.value().chunks[0].coord, (arv::ChunkSectionCoord{{0, 0, 0}}));
  EXPECT_EQ(loaded.value().chunks[0].blocks[0], stone);
  EXPECT_TRUE(loaded.value().chunks[0].fluids[0].source());
  ASSERT_EQ(loaded.value().players.size(), 1U);
  EXPECT_EQ(loaded.value().players[0].profile_name, "local_player");
  ASSERT_TRUE(loaded.value().players[0].respawn_location.has_value());
  EXPECT_EQ(
      arv::location_position(*loaded.value().players[0].respawn_location),
      (arv::Vec3f{40.5F, 71.0F, -12.5F}));
  EXPECT_FLOAT_EQ(loaded.value().players[0].respawn_location->yaw_radians, 0.75F);
  EXPECT_FLOAT_EQ(loaded.value().players[0].respawn_location->pitch_radians, -0.125F);
  EXPECT_EQ(loaded.value().players[0].game_mode_id, arv::player::CreativeGameModeID);
  EXPECT_EQ(loaded.value().players[0].permission_overrides.groups,
            (std::vector<std::string>{"admin", "builder"}));
  EXPECT_EQ(loaded.value().players[0].permission_overrides.overrides.can_fly,
            arv::player::TriState::Allow);
  EXPECT_EQ(
      loaded.value().players[0]
          .permission_overrides.custom_overrides.at("arvane.command.test"),
      arv::player::TriState::Deny);
  EXPECT_EQ(loaded.value().players[0].selected_hotbar_slot, 3U);
  ASSERT_TRUE(loaded.value().players[0].inventory.slot(0).has_value());
  EXPECT_EQ(loaded.value().players[0].inventory.slot(0)->item, stone_item);
  EXPECT_EQ(loaded.value().players[0].inventory.slot(0)->count, 7U);
  ASSERT_TRUE(loaded.value().players[0].inventory.slot(0)->data.has_value());
  const arv::io::DataValue* player_stack_data =
      arv::io::find_path(*loaded.value().players[0].inventory.slot(0)->data, "test.value");
  ASSERT_NE(player_stack_data, nullptr);
  EXPECT_EQ(player_stack_data->as_string_or({}), "player");
  ASSERT_EQ(loaded.value().entities.size(), 3U);
  const auto entity_a = std::find_if(
      loaded.value().entities.begin(),
      loaded.value().entities.end(),
      [](const arv::entity::PersistentEntityRecord& record) {
        return record.uuid == "entity-test-uuid-a";
      });
  ASSERT_NE(entity_a, loaded.value().entities.end());
  EXPECT_EQ(entity_a->type_name, "arvane:test_entity");
  EXPECT_EQ(entity_a->world_id, (arv::WorldID{1}));
  EXPECT_EQ(entity_a->position, (arv::Vec3f{4.0F, 5.0F, 6.0F}));
  EXPECT_EQ(entity_a->velocity, (arv::Vec3f{0.1F, 0.2F, 0.3F}));
  const auto entity_b = std::find_if(
      loaded.value().entities.begin(),
      loaded.value().entities.end(),
      [](const arv::entity::PersistentEntityRecord& record) {
        return record.uuid == "entity-test-uuid-b";
      });
  ASSERT_NE(entity_b, loaded.value().entities.end());
  EXPECT_EQ(entity_b->position, (arv::Vec3f{-1.0F, 5.0F, -1.0F}));
  EXPECT_EQ(entity_b->velocity, (arv::Vec3f{-0.1F, 0.0F, 0.1F}));
  const auto dropped_item = std::find_if(
      loaded.value().entities.begin(),
      loaded.value().entities.end(),
      [](const arv::entity::PersistentEntityRecord& record) {
        return record.uuid == "dropped-item-uuid";
      });
  ASSERT_NE(dropped_item, loaded.value().entities.end());
  EXPECT_EQ(dropped_item->type_name, arv::entity::DroppedItemEntityTypeName);
  ASSERT_TRUE(dropped_item->dropped_item_stack.has_value());
  EXPECT_EQ(dropped_item->dropped_item_stack->item, stone_item);
  EXPECT_EQ(dropped_item->dropped_item_stack->count, 4U);
  ASSERT_TRUE(dropped_item->dropped_item_stack->data.has_value());
  const arv::io::DataValue* dropped_stack_data =
      arv::io::find_path(*dropped_item->dropped_item_stack->data, "test.value");
  ASSERT_NE(dropped_stack_data, nullptr);
  EXPECT_EQ(dropped_stack_data->as_string_or({}), "drop");
  EXPECT_FLOAT_EQ(dropped_item->dropped_item_pickup_delay_seconds, 0.25F);

  {
    const std::filesystem::path world_path =
        server_save_root / "worlds" / "default" / "world.json";
    std::ifstream input(world_path);
    ASSERT_TRUE(input);
    std::string world_json{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}};
    const std::string current_required = R"json(    "required": [
      "arvane_base",
      "test_pack"
    ])json";
    const std::string legacy_required = R"json(    "required": [
      {
        "content_hash": 123456789,
        "id": "arvane_base",
        "namespaces": [
          "arvane"
        ],
        "native_client_code_required": false,
        "side": "both",
        "version": "0.0.0"
      },
      {
        "content_hash": 987654321,
        "id": "test_pack",
        "version": "0.0.0"
      }
    ])json";
    const std::size_t required_offset = world_json.find(current_required);
    ASSERT_NE(required_offset, std::string::npos);
    world_json.replace(required_offset, current_required.size(), legacy_required);

    std::ofstream output(world_path, std::ios::trunc);
    ASSERT_TRUE(output);
    output << world_json;
  }

  auto legacy_loaded = arv::persistence::load_server_root(
      server_save_root,
      block_registry,
      item_registry);
  ASSERT_TRUE(legacy_loaded);
  ASSERT_EQ(legacy_loaded.value().world.required_packs.size(), 2U);
  EXPECT_EQ(legacy_loaded.value().world.required_packs[0].id, "arvane_base");
  EXPECT_EQ(legacy_loaded.value().world.required_packs[1].id, "test_pack");
}

TEST(SaveTests, WorldHeightLoadsFromGenerationBoundsWhenChunkBoundsAreStale) {
  TempDir temp_dir;
  const std::filesystem::path server_save_root =
      temp_dir.path / "arvane_home" / "saves" / "server-save-1";
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);
  const arv::voxel::BlockVariantID stone = block_registry.find_variant("arvane:stone").value();

  arv::world::WorldGenerationConfig generation_config = make_worldgen_config();
  generation_config.min_y = -32;
  generation_config.max_y = 96;

  const arv::persistence::SaveMetadata save_metadata{
      arv::persistence::CurrentSaveFormatVersion,
      std::string{arv::persistence::CurrentGameVersion},
      arv::persistence::hash_block_registry(block_registry),
      arv::persistence::hash_item_registry(item_registry),
      "Test Save"};
  const arv::persistence::WorldMetadata world_metadata{
      "default",
      42,
      99,
      generation_config,
      -2,
      1,
      save_metadata.block_registry_hash,
      save_metadata.item_registry_hash};
  const std::vector<arv::persistence::SavedChunkSection> chunks{
      make_saved_chunk({{0, 6, 0}}, stone)};

  ASSERT_TRUE(arv::persistence::save_server_root(
      server_save_root,
      save_metadata,
      world_metadata,
      chunks,
      std::span<const arv::persistence::PlayerSaveData>{},
      std::span<const arv::entity::PersistentEntityRecord>{},
      block_registry,
      item_registry));

  const std::filesystem::path region_file =
      server_save_root / "worlds" / "default" / "regions" / "r.0.0.arvr";
  ASSERT_TRUE(arv::persistence::write_region_file(
      region_file,
      arv::RegionCoord{0, 0},
      chunks,
      block_registry,
      -1));

  const std::filesystem::path world_file =
      server_save_root / "worlds" / "default" / "world.json";
  std::string world_text;
  {
    std::ifstream input(world_file);
    ASSERT_TRUE(input);
    world_text.assign(
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{});
  }
  const std::string saved_min_chunk_y = "\"min_chunk_y\": -2";
  const std::string saved_max_chunk_y = "\"max_chunk_y\": 6";
  const std::string::size_type min_chunk_y_pos = world_text.find(saved_min_chunk_y);
  const std::string::size_type max_chunk_y_pos = world_text.find(saved_max_chunk_y);
  ASSERT_NE(min_chunk_y_pos, std::string::npos);
  ASSERT_NE(max_chunk_y_pos, std::string::npos);
  world_text.replace(min_chunk_y_pos, saved_min_chunk_y.size(), "\"min_chunk_y\": -1");
  world_text.replace(max_chunk_y_pos, saved_max_chunk_y.size(), "\"max_chunk_y\": 1");
  {
    std::ofstream output(world_file, std::ios::trunc);
    ASSERT_TRUE(output);
    output << world_text;
  }

  auto loaded = arv::persistence::load_server_root(
      server_save_root,
      block_registry,
      item_registry);

  ASSERT_TRUE(loaded);
  ASSERT_TRUE(loaded.value().has_world_metadata);
  EXPECT_EQ(loaded.value().world.min_chunk_y, -2);
  EXPECT_EQ(loaded.value().world.max_chunk_y, 6);
  ASSERT_EQ(loaded.value().chunks.size(), 1U);
  EXPECT_EQ(loaded.value().chunks[0].coord, (arv::ChunkSectionCoord{{0, 6, 0}}));

  arv::world::WorldRuntime world{arv::WorldID{1}};
  world.configure_height({loaded.value().world.min_chunk_y, loaded.value().world.max_chunk_y});
  world.restore_saved_chunk(
      loaded.value().chunks[0].coord,
      loaded.value().chunks[0].blocks,
      loaded.value().chunks[0].lights,
      loaded.value().chunks[0].fluids,
      loaded.value().chunks[0].lighting_valid);
  EXPECT_NE(world.find_chunk({{0, 6, 0}}), nullptr);
}

TEST(SaveTests, PlayerPermissionsAreWrittenEvenWhenEmpty) {
  TempDir temp_dir;
  const std::filesystem::path server_save_root =
      temp_dir.path / "arvane_home" / "saves" / "server-save-1";
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);

  arv::persistence::PlayerSaveData player;
  player.profile_name = "local_player";
  player.world_name = "default";
  player.position = {1.0F, 2.0F, 3.0F};

  const arv::persistence::SaveMetadata save_metadata{
      arv::persistence::CurrentSaveFormatVersion,
      std::string{arv::persistence::CurrentGameVersion},
      arv::persistence::hash_block_registry(block_registry),
      arv::persistence::hash_item_registry(item_registry),
      "Test Save"};
  const arv::persistence::WorldMetadata world_metadata{
      "default",
      42,
      99,
      make_worldgen_config(),
      0,
      3,
      save_metadata.block_registry_hash,
      save_metadata.item_registry_hash};
  const std::vector<arv::persistence::PlayerSaveData> players{player};

  ASSERT_TRUE(arv::persistence::save_server_root(
      server_save_root,
      save_metadata,
      world_metadata,
      std::span<const arv::persistence::SavedChunkSection>{},
      players,
      std::span<const arv::entity::PersistentEntityRecord>{},
      block_registry,
      item_registry));

  std::ifstream input(server_save_root / "players" / "local_player.dat");
  ASSERT_TRUE(input.is_open());
  const std::string text{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
  EXPECT_NE(text.find("\"groups\": []"), std::string::npos);
  EXPECT_NE(text.find("\"permission_overrides\""), std::string::npos);
  EXPECT_NE(text.find("\"core\": {}"), std::string::npos);
  EXPECT_NE(text.find("\"permissions\": {}"), std::string::npos);

  std::ifstream permissions_input(server_save_root / "permissions.yaml");
  ASSERT_TRUE(permissions_input.is_open());
  const std::string permissions_text{
      std::istreambuf_iterator<char>{permissions_input},
      std::istreambuf_iterator<char>{}};
  EXPECT_NE(permissions_text.find("groups: {}"), std::string::npos);
}

TEST(SaveTests, DiscoversServerSavesSeparatelyFromWorldsAndLoadsRequestedWorld) {
  TempDir temp_dir;
  const std::filesystem::path saves_root = temp_dir.path / "arvane_home" / "saves";
  const std::filesystem::path server_save_root = saves_root / "server-save-1";
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);

  const arv::persistence::SaveMetadata save_metadata{
      arv::persistence::CurrentSaveFormatVersion,
      std::string{arv::persistence::CurrentGameVersion},
      arv::persistence::hash_block_registry(block_registry),
      arv::persistence::hash_item_registry(item_registry)};
  auto save_metadata_one = save_metadata;
  save_metadata_one.name = "Server One";
  auto save_metadata_two = save_metadata;
  save_metadata_two.name = "Server Two";
  const arv::world::WorldGenerationConfig config = make_worldgen_config();
  const std::vector<arv::persistence::SavedChunkSection> chunks{};
  const std::vector<arv::persistence::PlayerSaveData> players{};
  const std::vector<arv::entity::PersistentEntityRecord> entities{};

  arv::persistence::WorldMetadata default_world{
      "default",
      11,
      1,
      config,
      0,
      3,
      save_metadata.block_registry_hash,
      save_metadata.item_registry_hash};
  default_world.spawn_point =
      arv::Location{3.5F, 44.0F, -9.5F, 0.5F, 0.125F};
  ASSERT_TRUE(arv::persistence::save_server_root(
      server_save_root,
      save_metadata_one,
      default_world,
      chunks,
      players,
      entities,
      block_registry,
      item_registry));
  ASSERT_TRUE(arv::persistence::save_server_root(
      server_save_root,
      save_metadata_one,
      {"sky", 22, 2, config, 0, 4, save_metadata.block_registry_hash, save_metadata.item_registry_hash},
      chunks,
      players,
      entities,
      block_registry,
      item_registry));
  ASSERT_TRUE(arv::persistence::save_server_root(
      saves_root / "server-save-2",
      save_metadata_two,
      {"default", 33, 3, config, 0, 5, save_metadata.block_registry_hash, save_metadata.item_registry_hash},
      chunks,
      players,
      entities,
      block_registry,
      item_registry));

  const std::vector<arv::persistence::SavedServerSummary> servers =
      arv::persistence::discover_saved_servers(saves_root);
  ASSERT_EQ(servers.size(), 2U);
  EXPECT_EQ(servers[0].name, "Server One");
  EXPECT_EQ(servers[0].server_save_name, "server-save-1");
  EXPECT_EQ(servers[0].primary_world_name, "default");
  EXPECT_EQ(servers[1].name, "Server Two");
  EXPECT_EQ(servers[1].server_save_name, "server-save-2");
  EXPECT_EQ(servers[1].primary_world_name, "default");

  const std::vector<arv::persistence::SavedWorldSummary> worlds =
      arv::persistence::discover_saved_worlds(saves_root);
  ASSERT_EQ(worlds.size(), 3U);
  EXPECT_EQ(worlds[0].server_save_name, "server-save-1");
  EXPECT_EQ(worlds[0].world_name, "default");
  EXPECT_EQ(worlds[1].server_save_name, "server-save-1");
  EXPECT_EQ(worlds[1].world_name, "sky");
  EXPECT_EQ(worlds[2].server_save_name, "server-save-2");
  EXPECT_EQ(worlds[2].world_name, "default");

  auto loaded = arv::persistence::load_server_root(
      server_save_root,
      "sky",
      block_registry,
      item_registry);
  ASSERT_TRUE(loaded);
  EXPECT_EQ(loaded.value().save.name, "Server One");
  ASSERT_TRUE(loaded.value().has_world_metadata);
  EXPECT_EQ(loaded.value().world.name, "sky");
  EXPECT_EQ(loaded.value().world.seed, 22U);
  EXPECT_EQ(loaded.value().world.max_chunk_y, 4);
  ASSERT_TRUE(loaded.value().world.spawn_point.has_value());
  EXPECT_EQ(
      arv::location_position(*loaded.value().world.spawn_point),
      (arv::Vec3f{3.5F, 44.0F, -9.5F}));
  EXPECT_FLOAT_EQ(loaded.value().world.spawn_point->yaw_radians, 0.5F);
  EXPECT_FLOAT_EQ(loaded.value().world.spawn_point->pitch_radians, 0.125F);
}

TEST(SaveTests, CorruptedSaveJsonReturnsFailureInsteadOfSilentlyLoading) {
  TempDir temp_dir;
  const arv::voxel::BlockRegistry block_registry = arv::testsupport::make_test_block_registry();
  const arv::player::ItemRegistry item_registry =
      arv::testsupport::make_test_item_registry(block_registry);

  std::ofstream output(temp_dir.path / "save.json", std::ios::trunc);
  ASSERT_TRUE(output.is_open());
  output << "{ this is not valid json";
  output.close();

  auto loaded = arv::persistence::load_server_root(
      temp_dir.path,
      block_registry,
      item_registry);

  ASSERT_FALSE(loaded);
  EXPECT_NE(loaded.error().message.find("parse"), std::string::npos);
}
