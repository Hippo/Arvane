#include "arv/world/world_runtime.hpp"

#include <gtest/gtest.h>

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
  EXPECT_EQ(world.entity_chunk(entity).value(), (arv::ChunkCoord{{1, 0, -1}}));
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
