#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/player/permissions.hpp"

#include <gtest/gtest.h>

#include <utility>
#include <vector>

TEST(PermissionTests, BuiltInGameModesResolveDocumentedCorePermissions) {
  arv::player::GameModeRegistry modes;
  arv::base::register_base_game_modes(modes);

  const auto creative = modes.permissions_for(arv::base::ids::CreativeGameModeID);
  ASSERT_TRUE(creative.has_value());
  EXPECT_FALSE(creative->can_take_damage);
  EXPECT_TRUE(creative->can_modify_world);
  EXPECT_TRUE(creative->can_fly);
  EXPECT_TRUE(creative->has_collision);

  const auto spectator = modes.permissions_for(arv::base::ids::SpectatorGameModeID);
  ASSERT_TRUE(spectator.has_value());
  EXPECT_FALSE(spectator->can_modify_world);
  EXPECT_FALSE(spectator->can_interact);
  EXPECT_TRUE(spectator->can_fly);
  EXPECT_TRUE(spectator->has_collision);

  const auto noclip = modes.permissions_for(arv::base::ids::NoClipGameModeID);
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
  arv::player::GameModeRegistry modes;
  arv::base::register_base_game_modes(modes);
  const arv::player::PermissionGroupRegistry groups;

  arv::player::PlayerPermissions player_permissions;
  player_permissions.overrides.can_modify_world = arv::player::TriState::Allow;

  arv::player::PermissionLayer region;
  region.core.can_modify_world = arv::player::TriState::Deny;

  const arv::player::ResolvedPermissions resolved = arv::player::resolve_permissions(
      modes,
      groups,
      {
          arv::base::ids::SpectatorGameModeID,
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

TEST(PermissionTests, GlobalWildcardGrantsAllCustomPermissions) {
  arv::player::CustomPermissionMap permissions;
  permissions["*"] = arv::player::TriState::Allow;
  permissions["arvane.command.shutdown"] = arv::player::TriState::Deny;

  EXPECT_TRUE(arv::player::is_valid_custom_permission_node("*"));
  EXPECT_EQ(
      arv::player::custom_permission_value(permissions, "plugin.shop.buy"),
      arv::player::TriState::Allow);
  EXPECT_EQ(
      arv::player::custom_permission_value(permissions, "arvane.command.gamemode.creative"),
      arv::player::TriState::Allow);
  EXPECT_EQ(
      arv::player::custom_permission_value(permissions, "arvane.command.shutdown"),
      arv::player::TriState::Deny);
}

TEST(PermissionTests, GameModeCustomPermissionsSeedResolvedCustomMap) {
  arv::player::GameModeRegistry modes;
  ASSERT_TRUE(modes.add_builtin_mode(
      std::string{arv::player::SurvivalGameModeID},
      {}).has_value());
  ASSERT_TRUE(modes.add_custom_mode(
      "builder",
      std::string{arv::player::SurvivalGameModeID},
      {},
      {{"plugin.build", arv::player::TriState::Allow}}).has_value());

  const arv::player::PermissionGroupRegistry groups;
  const arv::player::ResolvedPermissions resolved = arv::player::resolve_permissions(
      modes,
      groups,
      {
          "builder",
          nullptr,
          {},
          nullptr,
          {},
      });

  EXPECT_EQ(
      resolved.custom_value("plugin.build"),
      arv::player::TriState::Allow);
}
