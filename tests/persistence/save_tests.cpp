#include "arv/persistence/save.hpp"

#include "arv/world/world_generation.hpp"
#include "support/test_registries.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
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
    arv::ChunkCoord coord,
    arv::voxel::BlockVariantID fill_variant) {
  arv::persistence::SavedChunkSection chunk;
  chunk.coord = coord;
  chunk.blocks.fill(fill_variant);
  chunk.lights.fill(arv::voxel::pack_light(0, 0, 0, 15));
  chunk.fluids.fill(arv::voxel::empty_fluid_cell());
  chunk.fluids[0] = arv::voxel::make_fluid_source(arv::voxel::make_water_fluid_definition());
  return chunk;
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
  player.game_mode_id = std::string{arv::player::CreativeGameModeID};
  player.selected_hotbar_slot = 3;
  ASSERT_TRUE(player.inventory.set_slot(0, arv::player::ItemStack{stone_item, 7}));

  const arv::persistence::SaveMetadata save_metadata{
      arv::persistence::CurrentSaveFormatVersion,
      std::string{arv::persistence::CurrentGameVersion},
      arv::persistence::hash_block_registry(block_registry),
      arv::persistence::hash_item_registry(item_registry)};
  const arv::persistence::WorldMetadata world_metadata{
      "default",
      42,
      99,
      make_worldgen_config(),
      0,
      3,
      save_metadata.block_registry_hash,
      save_metadata.item_registry_hash};
  const std::vector<arv::persistence::SavedChunkSection> chunks{
      make_saved_chunk({{0, 0, 0}}, stone)};
  const std::vector<arv::persistence::PlayerSaveData> players{player};
  const std::vector<arv::entity::PersistentEntityRecord> entities{
      {
          "entity-test-uuid-a",
          "arvane:test_entity",
          arv::WorldID{1},
          {4.0F, 5.0F, 6.0F},
          {0.0F, 0.0F, 0.0F, 1.0F},
          {0.1F, 0.2F, 0.3F}},
      {
          "entity-test-uuid-b",
          "arvane:test_entity",
          arv::WorldID{1},
          {-1.0F, 5.0F, -1.0F},
          {0.0F, 0.0F, 0.0F, 1.0F},
          {-0.1F, 0.0F, 0.1F}}};

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
  EXPECT_TRUE(std::filesystem::exists(server_save_root / "players" / "local_player.dat"));
  EXPECT_TRUE(std::filesystem::exists(server_save_root / "worlds" / "default" / "world.json"));
  EXPECT_FALSE(std::filesystem::exists(server_save_root / "worlds" / "default" / "entities.json"));
  EXPECT_TRUE(std::filesystem::exists(
      server_save_root / "worlds" / "default" / "entities" / "r.0.0.json"));
  EXPECT_TRUE(std::filesystem::exists(
      server_save_root / "worlds" / "default" / "entities" / "r.-1.-1.json"));

  auto loaded = arv::persistence::load_server_root(
      server_save_root,
      block_registry,
      item_registry);

  ASSERT_TRUE(loaded);
  EXPECT_TRUE(loaded.value().has_save_metadata);
  EXPECT_TRUE(loaded.value().has_world_metadata);
  EXPECT_EQ(loaded.value().save.save_format_version, arv::persistence::CurrentSaveFormatVersion);
  EXPECT_EQ(loaded.value().world.seed, 42U);
  EXPECT_EQ(loaded.value().world.tick_index, 99U);
  ASSERT_EQ(loaded.value().chunks.size(), 1U);
  EXPECT_EQ(loaded.value().chunks[0].coord, (arv::ChunkCoord{{0, 0, 0}}));
  EXPECT_EQ(loaded.value().chunks[0].blocks[0], stone);
  EXPECT_TRUE(loaded.value().chunks[0].fluids[0].source());
  ASSERT_EQ(loaded.value().players.size(), 1U);
  EXPECT_EQ(loaded.value().players[0].profile_name, "local_player");
  EXPECT_EQ(loaded.value().players[0].game_mode_id, arv::player::CreativeGameModeID);
  EXPECT_EQ(loaded.value().players[0].selected_hotbar_slot, 3U);
  ASSERT_TRUE(loaded.value().players[0].inventory.slot(0).has_value());
  EXPECT_EQ(loaded.value().players[0].inventory.slot(0)->item, stone_item);
  EXPECT_EQ(loaded.value().players[0].inventory.slot(0)->count, 7U);
  ASSERT_EQ(loaded.value().entities.size(), 2U);
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
