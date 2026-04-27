#include "arv/player/permissions.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

TEST(PermissionTests, BuiltInGameModesResolveDocumentedCorePermissions) {
  const arv::player::GameModeRegistry modes = arv::player::GameModeRegistry::with_builtins();

  const auto creative = modes.permissions_for(arv::player::CreativeGameModeID);
  ASSERT_TRUE(creative.has_value());
  EXPECT_FALSE(creative->can_take_damage);
  EXPECT_TRUE(creative->can_modify_world);
  EXPECT_TRUE(creative->can_fly);
  EXPECT_TRUE(creative->has_collision);

  const auto spectator = modes.permissions_for(arv::player::SpectatorGameModeID);
  ASSERT_TRUE(spectator.has_value());
  EXPECT_FALSE(spectator->can_modify_world);
  EXPECT_FALSE(spectator->can_interact);
  EXPECT_TRUE(spectator->can_fly);
  EXPECT_TRUE(spectator->has_collision);

  const auto noclip = modes.permissions_for(arv::player::NoClipGameModeID);
  ASSERT_TRUE(noclip.has_value());
  EXPECT_TRUE(noclip->can_fly);
  EXPECT_FALSE(noclip->has_collision);
  EXPECT_FALSE(noclip->is_visible_to_others);
}

TEST(PermissionTests, PlayerGroupsExpandInheritanceAndRespectDenyPrecedence) {
  arv::player::PermissionGroupRegistry groups;

  arv::player::PermissionGroup player;
  player.id = "player";
  player.priority = 0;
  player.custom["plugin.shop.buy"] = arv::player::TriState::Allow;
  ASSERT_TRUE(groups.add_group(std::move(player)));

  arv::player::PermissionGroup restricted;
  restricted.id = "restricted";
  restricted.priority = 100;
  restricted.custom["plugin.shop.buy"] = arv::player::TriState::Deny;
  ASSERT_TRUE(groups.add_group(std::move(restricted)));

  arv::player::PermissionGroup mod;
  mod.id = "mod";
  mod.priority = 50;
  mod.inherits.push_back("player");
  mod.custom["arvane.command.kick"] = arv::player::TriState::Allow;
  ASSERT_TRUE(groups.add_group(std::move(mod)));

  const std::vector<std::string> assigned{"mod", "restricted"};
  auto layer = groups.resolve_groups(assigned);
  ASSERT_TRUE(layer);
  EXPECT_EQ(
      arv::player::custom_permission_value(layer.value().custom, "arvane.command.kick"),
      arv::player::TriState::Allow);
  EXPECT_EQ(
      arv::player::custom_permission_value(layer.value().custom, "plugin.shop.buy"),
      arv::player::TriState::Deny);
}

TEST(PermissionTests, RuntimeOverridesCanAllowAboveGameModeBaseButRegionDenyWins) {
  const arv::player::GameModeRegistry modes = arv::player::GameModeRegistry::with_builtins();
  const arv::player::PermissionGroupRegistry groups;

  arv::player::PlayerPermissions player_permissions;
  player_permissions.overrides.can_modify_world = arv::player::TriState::Allow;

  arv::player::PermissionLayer region;
  region.core.can_modify_world = arv::player::TriState::Deny;

  const arv::player::ResolvedPermissions resolved = arv::player::resolve_permissions(
      modes,
      groups,
      {
          arv::player::SpectatorGameModeID,
          &player_permissions,
          {},
          &region,
          {},
      });

  EXPECT_FALSE(resolved.core.can_modify_world);
  EXPECT_TRUE(resolved.core.can_fly);
}

TEST(PermissionTests, CustomPermissionWildcardsPreferSpecificMatches) {
  arv::player::CustomPermissionMap permissions;
  permissions["arvane.*"] = arv::player::TriState::Allow;
  permissions["arvane.command.*"] = arv::player::TriState::Deny;
  permissions["arvane.command.gamemode"] = arv::player::TriState::Allow;

  EXPECT_EQ(
      arv::player::custom_permission_value(permissions, "arvane.chat.send"),
      arv::player::TriState::Allow);
  EXPECT_EQ(
      arv::player::custom_permission_value(permissions, "arvane.command.tp"),
      arv::player::TriState::Deny);
  EXPECT_EQ(
      arv::player::custom_permission_value(permissions, "arvane.command.gamemode"),
      arv::player::TriState::Allow);
}
