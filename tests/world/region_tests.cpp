#include "arv/world/regions.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

TEST(RegionTests, FindsOnlyRegionsInMatchingWorldAndAabb) {
  arv::world::RegionRegistry regions;

  arv::world::Region spawn;
  spawn.name = "spawn";
  spawn.world = arv::WorldID{1};
  spawn.shape.min = {-4.0F, 0.0F, -4.0F};
  spawn.shape.max = {4.0F, 8.0F, 4.0F};
  const arv::world::RegionID spawn_id = regions.add_region(std::move(spawn));
  ASSERT_NE(spawn_id.value, 0U);

  arv::world::Region other_world;
  other_world.name = "other_world";
  other_world.world = arv::WorldID{2};
  other_world.shape.min = {-4.0F, 0.0F, -4.0F};
  other_world.shape.max = {4.0F, 8.0F, 4.0F};
  ASSERT_NE(regions.add_region(std::move(other_world)).value, 0U);

  const std::vector<arv::world::RegionID> matches =
      regions.find_regions_at({arv::WorldID{1}, {0.0F, 1.0F, 0.0F}});

  ASSERT_EQ(matches.size(), 1U);
  EXPECT_EQ(matches[0], spawn_id);
  EXPECT_TRUE(regions.find_regions_at({arv::WorldID{1}, {8.0F, 1.0F, 0.0F}}).empty());
}

TEST(RegionTests, RegionLocalRulesOverrideZoneRulesAndHigherPriorityWins) {
  arv::world::RegionRegistry regions;
  EXPECT_EQ(regions.revision(), 0U);

  arv::world::Zone lobby;
  lobby.name = "lobby";
  lobby.priority = 0;
  lobby.core.can_modify_world = arv::player::TriState::Deny;
  const arv::world::ZoneID lobby_id = regions.add_zone(std::move(lobby));
  ASSERT_NE(lobby_id.value, 0U);
  EXPECT_EQ(regions.revision(), 1U);

  arv::world::Region spawn;
  spawn.name = "spawn";
  spawn.world = arv::WorldID{1};
  spawn.priority = 10;
  spawn.shape.min = {-8.0F, 0.0F, -8.0F};
  spawn.shape.max = {8.0F, 16.0F, 8.0F};
  spawn.zones.push_back(lobby_id);
  ASSERT_NE(regions.add_region(std::move(spawn)).value, 0U);
  EXPECT_EQ(regions.revision(), 2U);

  arv::world::Region build_area;
  build_area.name = "spawn_build_area";
  build_area.world = arv::WorldID{1};
  build_area.priority = 100;
  build_area.shape.min = {-2.0F, 0.0F, -2.0F};
  build_area.shape.max = {2.0F, 16.0F, 2.0F};
  build_area.core.can_modify_world = arv::player::TriState::Allow;
  ASSERT_NE(regions.add_region(std::move(build_area)).value, 0U);
  EXPECT_EQ(regions.revision(), 3U);

  const arv::player::PermissionLayer layer =
      regions.permission_layer_at({arv::WorldID{1}, {0.0F, 1.0F, 0.0F}});

  EXPECT_EQ(layer.core.can_modify_world, arv::player::TriState::Allow);

  const std::vector<arv::world::RegionID> matching_ids =
      regions.find_regions_at({arv::WorldID{1}, {0.0F, 1.0F, 0.0F}});
  const arv::player::PermissionLayer cached_layer =
      regions.permission_layer_for_regions(matching_ids);
  EXPECT_EQ(cached_layer.core.can_modify_world, arv::player::TriState::Allow);
}

TEST(RegionTests, TracksPlayerRegionEntryAndExitSets) {
  arv::world::RegionRegistry regions;

  arv::world::Region spawn;
  spawn.name = "spawn";
  spawn.world = arv::WorldID{1};
  spawn.shape.min = {-1.0F, 0.0F, -1.0F};
  spawn.shape.max = {1.0F, 4.0F, 1.0F};
  const arv::world::RegionID spawn_id = regions.add_region(std::move(spawn));
  ASSERT_NE(spawn_id.value, 0U);

  arv::world::PlayerRegionState state;
  const arv::world::RegionMembershipChange entered =
      regions.update_membership(state, {arv::WorldID{1}, {0.0F, 1.0F, 0.0F}});
  ASSERT_EQ(entered.entered.size(), 1U);
  EXPECT_EQ(entered.entered[0], spawn_id);
  EXPECT_TRUE(entered.exited.empty());

  const arv::world::RegionMembershipChange exited =
      regions.update_membership(state, {arv::WorldID{1}, {5.0F, 1.0F, 0.0F}});
  EXPECT_TRUE(exited.entered.empty());
  ASSERT_EQ(exited.exited.size(), 1U);
  EXPECT_EQ(exited.exited[0], spawn_id);
}
