#include "arv/server/permission_config.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

struct TempDir {
  std::filesystem::path path{};

  TempDir() {
    path = std::filesystem::temp_directory_path() /
           ("arvane_permissions_test_" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(path);
  }

  ~TempDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

} // namespace

TEST(ServerPermissionConfigTests, ParsesPermissionGroupsYaml) {
  auto loaded = arv::server::parse_permission_groups_yaml(R"yaml(
groups:
  player:
    priority: 0
    core:
      can_open_inventory: ALLOW
    permissions:
      plugin.shop.buy: ALLOW
  mod:
    priority: 50
    inherits: [player]
    permissions:
      arvane.command.teleport: ALLOW
  admin:
    priority: 100
    inherits: [mod]
    core:
      can_modify_world: ALLOW
    permissions:
      arvane.*: ALLOW
      arvane.command.shutdown: DENY
)yaml");

  ASSERT_TRUE(loaded);
  const arv::player::PermissionGroup* admin = loaded.value().find("admin");
  ASSERT_NE(admin, nullptr);
  EXPECT_EQ(admin->priority, 100);

  const std::vector<std::string> groups{"admin"};
  auto layer = loaded.value().resolve_groups(groups);
  ASSERT_TRUE(layer);
  EXPECT_EQ(layer.value().core.can_open_inventory, arv::player::TriState::Allow);
  EXPECT_EQ(layer.value().core.can_modify_world, arv::player::TriState::Allow);
  EXPECT_EQ(
      arv::player::custom_permission_value(
          layer.value().custom,
          "arvane.command.teleport"),
      arv::player::TriState::Allow);
  EXPECT_EQ(
      arv::player::custom_permission_value(
          layer.value().custom,
          "arvane.command.shutdown"),
      arv::player::TriState::Deny);
}

TEST(ServerPermissionConfigTests, LoadsPermissionsYamlFromServerSaveRoot) {
  TempDir temp_dir;
  std::ofstream output(arv::server::permission_config_file_path(temp_dir.path));
  output << R"yaml(
groups:
  builder:
    permissions:
      arvane.command.build: ALLOW
)yaml";
  output.close();

  auto loaded = arv::server::load_server_permission_groups(temp_dir.path);

  ASSERT_TRUE(loaded);
  const std::vector<std::string> groups{"builder"};
  auto layer = loaded.value().resolve_groups(groups);
  ASSERT_TRUE(layer);
  EXPECT_EQ(
      arv::player::custom_permission_value(
          layer.value().custom,
          "arvane.command.build"),
      arv::player::TriState::Allow);
}
