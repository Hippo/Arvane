#include "arv_perms/permission_store.hpp"

#include <gtest/gtest.h>

namespace {

constexpr std::string_view ProfileA =
    "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

} // namespace

TEST(ArvPermsTests, LoadsDefaultStoreWithoutDocuments) {
  arv_perms::StoreLoadResult loaded =
      arv_perms::load_store_from_documents(nullptr, nullptr);

  ASSERT_TRUE(loaded.ok);
  EXPECT_TRUE(loaded.store.groups.contains("player"));
  EXPECT_TRUE(loaded.store.groups.contains("owner"));
  EXPECT_EQ(loaded.store.config.default_groups, std::vector<std::string>{"player"});
}

TEST(ArvPermsTests, ValidationRejectsMissingParentsAndInvalidPlayers) {
  arv_perms::PermissionStore store;
  store.config.default_groups = {"missing"};

  arv_perms::PermissionGroupRecord group;
  group.id = "admin";
  group.inherits = {"mod"};
  store.groups[group.id] = group;

  arv_perms::PlayerPermissionRecord player;
  player.player_id = "bad-profile";
  player.groups = {"admin", "admin"};
  store.players[player.player_id] = player;

  const arv_perms::ValidationResult validation = store.validate();

  ASSERT_FALSE(validation.ok());
  EXPECT_GE(validation.diagnostics.size(), 4U);
}

TEST(ArvPermsTests, ResolvesInheritancePriorityDenyAndDirectOverrides) {
  arv_perms::PermissionStore store;
  store.config.default_groups = {"player"};

  arv_perms::PermissionGroupRecord player;
  player.id = "player";
  player.priority = 0;
  player.permissions["arvane.command.spawn"] = arv::PermissionValue::Allow;
  store.groups[player.id] = player;

  arv_perms::PermissionGroupRecord admin;
  admin.id = "admin";
  admin.priority = 100;
  admin.inherits = {"player"};
  admin.permissions["arvane.command.*"] = arv::PermissionValue::Allow;
  store.groups[admin.id] = admin;

  arv_perms::PermissionGroupRecord restricted;
  restricted.id = "restricted";
  restricted.priority = 500;
  restricted.permissions["arvane.command.spawn"] = arv::PermissionValue::Deny;
  store.groups[restricted.id] = restricted;

  arv_perms::PlayerPermissionRecord record;
  record.player_id = std::string{ProfileA};
  record.groups = {"admin", "restricted"};
  record.permission_overrides["arvane.command.shutdown"] = arv::PermissionValue::Deny;

  const arv_perms::ResolvedPlayerOverrideLayer resolved =
      store.resolve_player(record);

  EXPECT_EQ(
      resolved.permissions.at("arvane.command.spawn"),
      arv::PermissionValue::Deny);
  EXPECT_EQ(
      resolved.permissions.at("arvane.command.*"),
      arv::PermissionValue::Allow);
  EXPECT_EQ(
      resolved.permissions.at("arvane.command.shutdown"),
      arv::PermissionValue::Deny);

  const arv_perms::PermissionTrace trace =
      store.trace_custom(record, "arvane.command.spawn");
  EXPECT_EQ(trace.player_overrides_result, arv::PermissionValue::Deny);
  ASSERT_FALSE(trace.group_lines.empty());
}

TEST(ArvPermsTests, EnsuresPlayersWithDefaultGroups) {
  arv_perms::PermissionStore store = arv_perms::default_store();
  arv_perms::PlayerPermissionRecord& player =
      store.ensure_player(std::string{ProfileA}, "example");
  player.permission_overrides["arvane.command.shutdown"] = arv::PermissionValue::Allow;

  const arv_perms::PlayerPermissionRecord* found = store.find_player(ProfileA);
  ASSERT_NE(found, nullptr);
  EXPECT_EQ(found->last_known_name, std::optional<std::string>{"example"});
  EXPECT_EQ(found->groups, std::vector<std::string>{"player"});
  EXPECT_EQ(
      found->permission_overrides.at("arvane.command.shutdown"),
      arv::PermissionValue::Allow);
}
