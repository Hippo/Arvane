#include "arv/io/data_codecs.hpp"
#include "arv/io/storage.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"

#include "arv/asset/pack.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <string_view>

namespace {

using arv::testsupport::ScopedTempDir;
using arv::testsupport::write_text;

struct CodecProbe {
  int x{};
  bool enabled{};
};

arv::io::ResourceKey key(std::string_view text) {
  auto parsed = arv::asset::ResourceID::parse(text);
  EXPECT_TRUE(parsed) << parsed.error();
  if (!parsed) {
    return {};
  }
  return {
      .name_space = parsed.value().name_space,
      .path = parsed.value().path};
}

} // namespace

namespace arv::io {

template<>
struct DataCodec<CodecProbe> {
  [[nodiscard]] static Expected<CodecProbe, DataDiagnostic> decode(
      const DataDocument& document,
      std::string_view path) {
    CodecProbe value;
    auto x = document.value_as<int>(std::string{path} + ".x");
    if (!x) {
      return Expected<CodecProbe, DataDiagnostic>::failure(x.error());
    }
    auto enabled = document.value_as<bool>(std::string{path} + ".enabled");
    if (!enabled) {
      return Expected<CodecProbe, DataDiagnostic>::failure(enabled.error());
    }
    value.x = x.value();
    value.enabled = enabled.value();
    return Expected<CodecProbe, DataDiagnostic>::success(value);
  }
};

} // namespace arv::io

TEST(DataDocumentTests, ConstructsNestedObjectsArraysAndCopiesDeeply) {
  arv::io::DataDocument document{7, arv::io::DataValue::object()};
  ASSERT_TRUE(arv::io::set_path(document, "limits.max_blocks", arv::u64{1000}));
  ASSERT_TRUE(arv::io::set_path(document, "enabled", true));
  ASSERT_TRUE(arv::io::set_path(document, "names", arv::io::DataValue::array()));
  document.root.get("names")->push("alpha");
  document.root.get("names")->push("beta");
  const arv::u8 bytes[] = {1, 2, 3};
  ASSERT_TRUE(arv::io::set_path(
      document,
      "payload",
      arv::io::DataValue::bytes(bytes)));

  EXPECT_EQ(document.schema_version, 7U);
  EXPECT_TRUE(arv::io::find_path(document, "enabled")->as_bool_or(false));
  EXPECT_EQ(arv::io::find_path(document, "limits.max_blocks")->as_u64_or(0), 1000U);
  EXPECT_EQ(arv::io::find_path(document, "names")->size(), 2U);
  EXPECT_EQ(arv::io::find_path(document, "missing"), nullptr);
  EXPECT_EQ(arv::io::DataValue{}.as_i64_or(42), 42);

  arv::io::DataDocument copy = document;
  ASSERT_TRUE(arv::io::set_path(document, "limits.max_blocks", arv::u64{2000}));
  EXPECT_EQ(arv::io::find_path(copy, "limits.max_blocks")->as_u64_or(0), 1000U);

  arv::io::DataDiagnostic diagnostic;
  EXPECT_EQ(arv::io::find_path(copy, "limits.missing", &diagnostic), nullptr);
  EXPECT_NE(diagnostic.describe().find("root.limits.missing"), std::string::npos);
}

TEST(DataCodecTests, YamlJsonAndBinaryRoundTripSchemaAndNestedData) {
  arv::io::DataDocument document{3, arv::io::DataValue::object()};
  ASSERT_TRUE(arv::io::set_path(document, "settings.enabled", true));
  ASSERT_TRUE(arv::io::set_path(document, "settings.limit", arv::i64{-5}));
  ASSERT_TRUE(arv::io::set_path(document, "names", arv::io::DataValue::array()));
  document.root.get("names")->push("alpha");
  document.root.get("names")->push("beta");
  const arv::u8 bytes[] = {10, 20, 30};
  ASSERT_TRUE(arv::io::set_path(document, "payload", arv::io::DataValue::bytes(bytes)));

  auto yaml = arv::io::encode_yaml(document);
  ASSERT_TRUE(yaml) << yaml.error().describe();
  auto yaml_decoded = arv::io::decode_yaml(yaml.value());
  ASSERT_TRUE(yaml_decoded) << yaml_decoded.error().describe();
  EXPECT_EQ(yaml_decoded.value().schema_version, 3U);
  EXPECT_TRUE(arv::io::find_path(yaml_decoded.value(), "settings.enabled")->as_bool_or(false));

  auto json = arv::io::encode_json(document);
  ASSERT_TRUE(json) << json.error().describe();
  auto json_decoded = arv::io::decode_json(json.value());
  ASSERT_TRUE(json_decoded) << json_decoded.error().describe();
  EXPECT_EQ(arv::io::find_path(json_decoded.value(), "payload")->bytes_if()->size(), 3U);

  auto binary = arv::io::encode_binary(document);
  ASSERT_TRUE(binary) << binary.error().describe();
  auto binary_decoded = arv::io::decode_binary(binary.value());
  ASSERT_TRUE(binary_decoded) << binary_decoded.error().describe();
  EXPECT_EQ(binary_decoded.value().schema_version, 3U);
  EXPECT_EQ(arv::io::find_path(binary_decoded.value(), "names")->at(1)->as_string_or({}), "beta");
}

TEST(DataCodecTests, InvalidDocumentsReturnDiagnostics) {
  auto yaml = arv::io::decode_yaml("schema_version: 1\n");
  ASSERT_FALSE(yaml);
  EXPECT_NE(yaml.error().describe().find("root.data"), std::string::npos);

  const arv::u8 invalid[] = {'b', 'a', 'd'};
  auto binary = arv::io::decode_binary(invalid);
  ASSERT_FALSE(binary);
  EXPECT_NE(binary.error().describe().find("root.magic"), std::string::npos);
}

TEST(DataCodecTests, ValueTemplateAndProxyDecodePrimitiveAndCustomTypes) {
  arv::io::DataDocument document{1, arv::io::DataValue::object()};
  ASSERT_TRUE(arv::io::set_path(document, "enabled", true));
  ASSERT_TRUE(arv::io::set_path(document, "count", arv::i64{42}));
  ASSERT_TRUE(arv::io::set_path(document, "probe.x", arv::i64{7}));
  ASSERT_TRUE(arv::io::set_path(document, "probe.enabled", false));

  auto enabled = document.value<bool>("enabled");
  ASSERT_TRUE(enabled) << enabled.error().describe();
  EXPECT_TRUE(enabled.value());

  arv::Expected<int, arv::io::DataDiagnostic> count = document.value("count");
  ASSERT_TRUE(count) << count.error().describe();
  EXPECT_EQ(count.value(), 42);

  auto probe = document.value_as<CodecProbe>("probe");
  ASSERT_TRUE(probe) << probe.error().describe();
  EXPECT_EQ(probe.value().x, 7);
  EXPECT_FALSE(probe.value().enabled);

  auto missing = document.value<CodecProbe>("missing");
  ASSERT_FALSE(missing);
  EXPECT_NE(missing.error().describe().find("root.missing"), std::string::npos);

  auto wrong_type = document.value<int>("enabled");
  ASSERT_FALSE(wrong_type);
  EXPECT_NE(wrong_type.error().describe().find("not an integer"), std::string::npos);
}

TEST(DataStorageTests, SafeResourcePathsRejectUnsafeKeys) {
  auto path = arv::io::safe_resource_relative_path(
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
  ScopedTempDir temp{"arv_io_config_"};
  arv::io::ConfigStorageService storage{temp.path / "config", temp.path / "save"};
  arv::io::DataDocument document{1, arv::io::DataValue::object()};
  ASSERT_TRUE(arv::io::set_path(document, "enabled", true));

  ASSERT_TRUE(storage.save_global_config("example_plugin", key("example_plugin:settings"), document));
  auto loaded = storage.load_global_config("example_plugin", key("example_plugin:settings"));
  ASSERT_TRUE(loaded) << loaded.error().describe();
  EXPECT_TRUE(arv::io::find_path(loaded.value(), "enabled")->as_bool_or(false));
  EXPECT_TRUE(std::filesystem::exists(
      temp.path / "config" / "plugins" / "example_plugin" / "settings.yaml"));

  auto missing = storage.load_global_config("example_plugin", key("example_plugin:missing"));
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().code, arv::io::DataStorageCode::NotFound);

  write_text(
      temp.path / "config" / "plugins" / "example_plugin" / "broken.yaml",
      "schema_version: 1\n");
  auto invalid = storage.load_global_config("example_plugin", key("example_plugin:broken"));
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().code, arv::io::DataStorageCode::Corrupt);

  ASSERT_TRUE(storage.save_world_config(arv::WorldID{2}, "example_plugin", key("example_plugin:settings"), document));
  EXPECT_TRUE(storage.exists_world_config(arv::WorldID{2}, "example_plugin", key("example_plugin:settings")));
}

TEST(DataStorageTests, PersistentScopesRoundTripAndRemoveIndividually) {
  ScopedTempDir temp{"arv_io_persistent_"};
  arv::io::PersistentDataStorageService storage{temp.path / "save"};
  arv::io::DataDocument document{4, arv::io::DataValue::object()};
  ASSERT_TRUE(arv::io::set_path(document, "counter", arv::u64{9}));

  const auto resource = key("example_plugin:data/probe");
  const arv::io::PersistentScope scopes[] = {
      {.kind = arv::io::PersistentScopeKind::Server},
      {.kind = arv::io::PersistentScopeKind::World, .world = arv::WorldID{1}},
      {.kind = arv::io::PersistentScopeKind::Player, .player = arv::PlayerID{7}, .player_stable_id = "profile-7"},
      {.kind = arv::io::PersistentScopeKind::Entity, .entity_uuid = "entity-uuid"},
      {.kind = arv::io::PersistentScopeKind::Block, .world = arv::WorldID{1}, .block_pos = {{17, 8, -3}}},
      {.kind = arv::io::PersistentScopeKind::Chunk, .world = arv::WorldID{1}, .chunk_pos = {2, -3}},
  };

  for (const arv::io::PersistentScope& scope : scopes) {
    ASSERT_TRUE(storage.save(scope, "example_plugin", resource, document));
    auto loaded = storage.load(scope, "example_plugin", resource);
    ASSERT_TRUE(loaded) << loaded.error().describe();
    EXPECT_EQ(loaded.value().schema_version, 4U);
    EXPECT_EQ(arv::io::find_path(loaded.value(), "counter")->as_u64_or(0), 9U);
    EXPECT_TRUE(storage.exists(scope, "example_plugin", resource));
  }

  ASSERT_TRUE(storage.remove(scopes[0], "example_plugin", resource));
  EXPECT_FALSE(storage.exists(scopes[0], "example_plugin", resource));
  EXPECT_TRUE(storage.exists(scopes[1], "example_plugin", resource));

  auto missing = storage.load(scopes[0], "example_plugin", resource);
  ASSERT_FALSE(missing);
  EXPECT_EQ(missing.error().code, arv::io::DataStorageCode::NotFound);

  EXPECT_TRUE(std::filesystem::exists(
      temp.path / "save" / "plugin_data" / "example_plugin" / "world" / "1" /
      "data" / "probe.bin"));
}
