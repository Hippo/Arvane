#include "arv/data/codecs.hpp"
#include "arv/data/storage.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string_view>

namespace {

struct ScopedTempDir {
  std::filesystem::path path;

  explicit ScopedTempDir(std::string_view prefix) {
    path = std::filesystem::temp_directory_path() /
           (std::string{prefix} +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path);
  }

  ~ScopedTempDir() {
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }
};

arv::asset::ResourceID key(std::string_view text) {
  auto parsed = arv::asset::ResourceID::parse(text);
  EXPECT_TRUE(parsed) << parsed.error();
  return parsed ? parsed.value() : arv::asset::ResourceID{};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::trunc);
  output << text;
}

} // namespace

TEST(DataDocumentTests, ConstructsNestedObjectsArraysAndCopiesDeeply) {
  arv::data::DataDocument document{7, arv::data::DataValue::object()};
  ASSERT_TRUE(arv::data::set_path(document, "limits.max_blocks", arv::u64{1000}));
  ASSERT_TRUE(arv::data::set_path(document, "enabled", true));
  ASSERT_TRUE(arv::data::set_path(document, "names", arv::data::DataValue::array()));
  document.root.get("names")->push("alpha");
  document.root.get("names")->push("beta");
  const arv::u8 bytes[] = {1, 2, 3};
  ASSERT_TRUE(arv::data::set_path(
      document,
      "payload",
      arv::data::DataValue::bytes(bytes)));

  EXPECT_EQ(document.schema_version, 7U);
  EXPECT_TRUE(arv::data::find_path(document, "enabled")->as_bool_or(false));
  EXPECT_EQ(arv::data::find_path(document, "limits.max_blocks")->as_u64_or(0), 1000U);
  EXPECT_EQ(arv::data::find_path(document, "names")->size(), 2U);
  EXPECT_EQ(arv::data::find_path(document, "missing"), nullptr);
  EXPECT_EQ(arv::data::DataValue{}.as_i64_or(42), 42);

  arv::data::DataDocument copy = document;
  ASSERT_TRUE(arv::data::set_path(document, "limits.max_blocks", arv::u64{2000}));
  EXPECT_EQ(arv::data::find_path(copy, "limits.max_blocks")->as_u64_or(0), 1000U);

  arv::data::DataDiagnostic diagnostic;
  EXPECT_EQ(arv::data::find_path(copy, "limits.missing", &diagnostic), nullptr);
  EXPECT_NE(diagnostic.describe().find("root.limits.missing"), std::string::npos);
}

TEST(DataCodecTests, YamlJsonAndBinaryRoundTripSchemaAndNestedData) {
  arv::data::DataDocument document{3, arv::data::DataValue::object()};
  ASSERT_TRUE(arv::data::set_path(document, "settings.enabled", true));
  ASSERT_TRUE(arv::data::set_path(document, "settings.limit", arv::i64{-5}));
  ASSERT_TRUE(arv::data::set_path(document, "names", arv::data::DataValue::array()));
  document.root.get("names")->push("alpha");
  document.root.get("names")->push("beta");
  const arv::u8 bytes[] = {10, 20, 30};
  ASSERT_TRUE(arv::data::set_path(document, "payload", arv::data::DataValue::bytes(bytes)));

  auto yaml = arv::data::encode_yaml(document);
  ASSERT_TRUE(yaml) << yaml.error().describe();
  auto yaml_decoded = arv::data::decode_yaml(yaml.value());
  ASSERT_TRUE(yaml_decoded) << yaml_decoded.error().describe();
  EXPECT_EQ(yaml_decoded.value().schema_version, 3U);
  EXPECT_TRUE(arv::data::find_path(yaml_decoded.value(), "settings.enabled")->as_bool_or(false));

  auto json = arv::data::encode_json(document);
  ASSERT_TRUE(json) << json.error().describe();
  auto json_decoded = arv::data::decode_json(json.value());
  ASSERT_TRUE(json_decoded) << json_decoded.error().describe();
  EXPECT_EQ(arv::data::find_path(json_decoded.value(), "payload")->bytes_if()->size(), 3U);

  auto binary = arv::data::encode_binary(document);
  ASSERT_TRUE(binary) << binary.error().describe();
  auto binary_decoded = arv::data::decode_binary(binary.value());
  ASSERT_TRUE(binary_decoded) << binary_decoded.error().describe();
  EXPECT_EQ(binary_decoded.value().schema_version, 3U);
  EXPECT_EQ(arv::data::find_path(binary_decoded.value(), "names")->at(1)->as_string_or({}), "beta");
}

TEST(DataCodecTests, InvalidDocumentsReturnDiagnostics) {
  auto yaml = arv::data::decode_yaml("schema_version: 1\n");
  ASSERT_FALSE(yaml);
  EXPECT_NE(yaml.error().describe().find("root.data"), std::string::npos);

  const arv::u8 invalid[] = {'b', 'a', 'd'};
  auto binary = arv::data::decode_binary(invalid);
  ASSERT_FALSE(binary);
  EXPECT_NE(binary.error().describe().find("root.magic"), std::string::npos);
}

TEST(DataStorageTests, SafeResourcePathsRejectUnsafeKeys) {
  auto path = arv::data::safe_resource_relative_path(
      "example_plugin",
      key("example_plugin:config/main"),
      ".yaml");
  ASSERT_TRUE(path) << path.error().describe();
  EXPECT_EQ(path.value().generic_string(), "config/main.yaml");

  EXPECT_FALSE(arv::asset::ResourceID::parse("example_plugin:../../escape"));
  EXPECT_FALSE(arv::asset::ResourceID::parse("example_plugin:/absolute"));
  EXPECT_FALSE(arv::asset::ResourceID::parse("example_plugin:C:\\escape"));
  EXPECT_FALSE(arv::asset::ResourceID::parse("example_plugin:config/../../../escape"));
}

TEST(DataStorageTests, ConfigStorageSavesLoadsAndReportsMissingOrInvalidYaml) {
  ScopedTempDir temp{"arv_data_config_"};
  arv::data::ConfigStorageService storage{temp.path / "config", temp.path / "save"};
  arv::data::DataDocument document{1, arv::data::DataValue::object()};
  ASSERT_TRUE(arv::data::set_path(document, "enabled", true));

  ASSERT_TRUE(storage.save_global_config("example_plugin", key("example_plugin:settings"), document));
  auto loaded = storage.load_global_config("example_plugin", key("example_plugin:settings"));
  ASSERT_TRUE(loaded) << loaded.error().describe();
  EXPECT_TRUE(arv::data::find_path(loaded.value(), "enabled")->as_bool_or(false));
  EXPECT_TRUE(std::filesystem::exists(
      temp.path / "config" / "plugins" / "example_plugin" / "settings.yaml"));

  auto missing = storage.load_global_config("example_plugin", key("example_plugin:missing"));
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().code, arv::data::DataStorageCode::NotFound);

  write_text(
      temp.path / "config" / "plugins" / "example_plugin" / "broken.yaml",
      "schema_version: 1\n");
  auto invalid = storage.load_global_config("example_plugin", key("example_plugin:broken"));
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().code, arv::data::DataStorageCode::Corrupt);

  ASSERT_TRUE(storage.save_world_config(arv::WorldID{2}, "example_plugin", key("example_plugin:settings"), document));
  EXPECT_TRUE(storage.exists_world_config(arv::WorldID{2}, "example_plugin", key("example_plugin:settings")));
}

TEST(DataStorageTests, PersistentScopesRoundTripAndRemoveIndividually) {
  ScopedTempDir temp{"arv_data_persistent_"};
  arv::data::PersistentDataStorageService storage{temp.path / "save"};
  arv::data::DataDocument document{4, arv::data::DataValue::object()};
  ASSERT_TRUE(arv::data::set_path(document, "counter", arv::u64{9}));

  const auto resource = key("example_plugin:data/probe");
  const arv::data::PersistentScope scopes[] = {
      {.kind = arv::data::PersistentScopeKind::Server},
      {.kind = arv::data::PersistentScopeKind::World, .world = arv::WorldID{1}},
      {.kind = arv::data::PersistentScopeKind::Player, .player = arv::PlayerID{7}, .player_stable_id = "profile-7"},
      {.kind = arv::data::PersistentScopeKind::Entity, .entity_uuid = "entity-uuid"},
      {.kind = arv::data::PersistentScopeKind::Block, .world = arv::WorldID{1}, .block_pos = {{17, 8, -3}}},
      {.kind = arv::data::PersistentScopeKind::Chunk, .world = arv::WorldID{1}, .chunk_pos = {2, -3}},
  };

  for (const arv::data::PersistentScope& scope : scopes) {
    ASSERT_TRUE(storage.save(scope, "example_plugin", resource, document));
    auto loaded = storage.load(scope, "example_plugin", resource);
    ASSERT_TRUE(loaded) << loaded.error().describe();
    EXPECT_EQ(loaded.value().schema_version, 4U);
    EXPECT_EQ(arv::data::find_path(loaded.value(), "counter")->as_u64_or(0), 9U);
    EXPECT_TRUE(storage.exists(scope, "example_plugin", resource));
  }

  ASSERT_TRUE(storage.remove(scopes[0], "example_plugin", resource));
  EXPECT_FALSE(storage.exists(scopes[0], "example_plugin", resource));
  EXPECT_TRUE(storage.exists(scopes[1], "example_plugin", resource));

  auto missing = storage.load(scopes[0], "example_plugin", resource);
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().code, arv::data::DataStorageCode::NotFound);

  EXPECT_TRUE(std::filesystem::exists(
      temp.path / "save" / "plugin_data" / "example_plugin" / "world" / "1" /
      "data" / "probe.bin"));
}
