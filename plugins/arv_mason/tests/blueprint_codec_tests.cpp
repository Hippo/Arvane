#include "arv_mason/blueprints/blueprint.hpp"

#include <gtest/gtest.h>

namespace {

arv_mason::Blueprint sample_blueprint() {
  arv_mason::Blueprint blueprint;
  blueprint.metadata.name = "bridge_test";
  blueprint.size = {4, 1, 1};
  blueprint.origin = {1, 0, 0};
  blueprint.palette = {"arvane:air[]", "arvane:stone[]", "arvane:dirt[]"};
  blueprint.blocks = {1, 1, 2, 0};
  return blueprint;
}

TEST(MasonBlueprintCodecTests, RoundTripsPaletteAndRle) {
  const arv_mason::Blueprint blueprint = sample_blueprint();

  const arv_mason::BlueprintCodecResult encoded =
      arv_mason::encode_blueprint(blueprint);
  ASSERT_TRUE(encoded.ok) << encoded.message;

  const arv_mason::BlueprintCodecResult decoded =
      arv_mason::decode_blueprint(encoded.text);
  ASSERT_TRUE(decoded.ok) << decoded.message;

  EXPECT_EQ(decoded.blueprint.metadata.name, "bridge_test");
  EXPECT_EQ(decoded.blueprint.size.x, 4);
  EXPECT_EQ(decoded.blueprint.palette.size(), 3U);
  EXPECT_EQ(decoded.blueprint.blocks, blueprint.blocks);
}

TEST(MasonBlueprintCodecTests, RejectsInvalidRleCount) {
  std::string text = arv_mason::encode_blueprint(sample_blueprint()).text;
  const std::string needle = "rle: 0 1\n";
  const std::size_t pos = text.rfind(needle);
  ASSERT_NE(pos, std::string::npos);
  text.erase(pos, needle.size());

  const arv_mason::BlueprintCodecResult decoded =
      arv_mason::decode_blueprint(text);

  EXPECT_FALSE(decoded.ok);
  EXPECT_NE(decoded.message.find("RLE"), std::string::npos);
}

TEST(MasonBlueprintCodecTests, RejectsInvalidSchema) {
  std::string text = arv_mason::encode_blueprint(sample_blueprint()).text;
  const std::size_t pos = text.find("schema_version: 1");
  ASSERT_NE(pos, std::string::npos);
  text.replace(pos, std::string{"schema_version: 1"}.size(), "schema_version: 2");

  const arv_mason::BlueprintCodecResult decoded =
      arv_mason::decode_blueprint(text);

  EXPECT_FALSE(decoded.ok);
}

TEST(MasonBlueprintCodecTests, ValidatesNames) {
  EXPECT_TRUE(arv_mason::valid_blueprint_name("tower_01.abp"));
  EXPECT_FALSE(arv_mason::valid_blueprint_name("../escape"));
  EXPECT_FALSE(arv_mason::valid_blueprint_name("name/with/path"));
  EXPECT_FALSE(arv_mason::valid_blueprint_name("C:\\escape"));
  EXPECT_FALSE(arv_mason::valid_blueprint_name(""));
}

TEST(MasonBlueprintCodecTests, BuildsBlueprintFromClipboard) {
  arv_mason::MasonClipboard clipboard;
  clipboard.size = {3, 1, 1};
  clipboard.source_min = {10, 20, 30};
  clipboard.origin = {11, 20, 30};
  clipboard.blocks = {
      {1, "arvane:stone[]"},
      {1, "arvane:stone[]"},
      {0, "arvane:air[]"},
  };

  const arv_mason::Blueprint blueprint =
      arv_mason::blueprint_from_clipboard("line", clipboard);

  EXPECT_EQ(blueprint.origin.x, 1);
  ASSERT_EQ(blueprint.palette.size(), 2U);
  EXPECT_EQ(blueprint.blocks[0], blueprint.blocks[1]);
  EXPECT_NE(blueprint.blocks[0], blueprint.blocks[2]);
}

} // namespace
