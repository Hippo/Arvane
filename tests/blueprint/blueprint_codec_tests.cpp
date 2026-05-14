#include "arv/blueprint/blueprint.hpp"

#include <gtest/gtest.h>

namespace {

arv::blueprint::Blueprint sample_blueprint() {
  arv::blueprint::Blueprint blueprint;
  blueprint.metadata.name = "bridge_test";
  blueprint.metadata.created_by = "arv_mason";
  blueprint.size = arv::BlockPos{{4, 1, 1}};
  blueprint.origin = arv::BlockPos{{1, 0, 0}};
  blueprint.palette = {"arvane:air[]", "arvane:stone[]", "arvane:dirt[]"};
  blueprint.blocks = {1, 1, 2, 0};
  return blueprint;
}

TEST(BlueprintCodecTests, RoundTripsPaletteAndRle) {
  const arv::blueprint::Blueprint blueprint = sample_blueprint();

  const arv::blueprint::BlueprintCodecResult encoded =
      arv::blueprint::encode_blueprint(blueprint);
  ASSERT_TRUE(encoded.ok) << encoded.message;

  const arv::blueprint::BlueprintCodecResult decoded =
      arv::blueprint::decode_blueprint(encoded.text);
  ASSERT_TRUE(decoded.ok) << decoded.message;

  EXPECT_EQ(decoded.blueprint.metadata.name, "bridge_test");
  EXPECT_EQ(decoded.blueprint.metadata.created_by, "arv_mason");
  EXPECT_EQ(decoded.blueprint.size.value.x, 4);
  EXPECT_EQ(decoded.blueprint.palette.size(), 3U);
  EXPECT_EQ(decoded.blueprint.blocks, blueprint.blocks);
}

TEST(BlueprintCodecTests, RejectsInvalidRleCount) {
  std::string text = arv::blueprint::encode_blueprint(sample_blueprint()).text;
  const std::string needle = "rle: 0 1\n";
  const std::size_t pos = text.rfind(needle);
  ASSERT_NE(pos, std::string::npos);
  text.erase(pos, needle.size());

  const arv::blueprint::BlueprintCodecResult decoded =
      arv::blueprint::decode_blueprint(text);

  EXPECT_FALSE(decoded.ok);
  EXPECT_NE(decoded.message.find("RLE"), std::string::npos);
}

TEST(BlueprintCodecTests, RejectsInvalidSchema) {
  std::string text = arv::blueprint::encode_blueprint(sample_blueprint()).text;
  const std::size_t pos = text.find("schema_version: 1");
  ASSERT_NE(pos, std::string::npos);
  text.replace(pos, std::string{"schema_version: 1"}.size(), "schema_version: 2");

  const arv::blueprint::BlueprintCodecResult decoded =
      arv::blueprint::decode_blueprint(text);

  EXPECT_FALSE(decoded.ok);
}

TEST(BlueprintCodecTests, ValidatesNames) {
  EXPECT_TRUE(arv::blueprint::valid_blueprint_name("tower_01.abp"));
  EXPECT_FALSE(arv::blueprint::valid_blueprint_name("../escape"));
  EXPECT_FALSE(arv::blueprint::valid_blueprint_name("name/with/path"));
  EXPECT_FALSE(arv::blueprint::valid_blueprint_name("C:\\escape"));
  EXPECT_FALSE(arv::blueprint::valid_blueprint_name(""));
}

TEST(BlueprintCodecTests, BuildsBlueprintFromClipboard) {
  arv::blueprint::BlueprintClipboard clipboard;
  clipboard.size = arv::BlockPos{{3, 1, 1}};
  clipboard.source_min = arv::BlockPos{{10, 20, 30}};
  clipboard.origin = arv::BlockPos{{11, 20, 30}};
  clipboard.blocks = {
      {1, "arvane:stone[]"},
      {1, "arvane:stone[]"},
      {0, "arvane:air[]"},
  };

  const arv::blueprint::Blueprint blueprint =
      arv::blueprint::blueprint_from_clipboard("line", clipboard, "arv_mason");

  EXPECT_EQ(blueprint.origin.value.x, 1);
  EXPECT_EQ(blueprint.metadata.created_by, "arv_mason");
  ASSERT_EQ(blueprint.palette.size(), 2U);
  EXPECT_EQ(blueprint.blocks[0], blueprint.blocks[1]);
  EXPECT_NE(blueprint.blocks[0], blueprint.blocks[2]);
}

TEST(BlueprintCodecTests, BuildsOwnerScopedStorageKeysWithoutChangingMasonNamespace) {
  EXPECT_EQ(
      arv::blueprint::blueprint_key("arv_mason", "tower"),
      "arv_mason:blueprints/tower");
}

} // namespace
