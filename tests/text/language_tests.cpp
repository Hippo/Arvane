#include "arv/text/language.hpp"

#include <gtest/gtest.h>

TEST(LanguageTests, ParsesNestedYamlAndFormatsStrings) {
  auto parsed = arv::text::parse_language_yaml(
      R"yaml(
ui:
  menu:
    greeting: Hello {name}
block:
  arvane:
    stone:
      display_name: Stone
)yaml",
      "en_US");

  ASSERT_TRUE(parsed);
  EXPECT_EQ(parsed.value().text("block.arvane.stone.display_name"), "Stone");
  EXPECT_EQ(
      parsed.value().format("ui.menu.greeting", {{"name", "Hippo"}}),
      "Hello Hippo");
  EXPECT_EQ(
      parsed.value().format(
          "ui.menu.missing",
          {{"name", "Hippo"}},
          "Fallback {name}"),
      "Fallback Hippo");
}

TEST(LanguageTests, BuildsDisplayNameKeysFromNamespacedIds) {
  EXPECT_EQ(
      arv::text::block_display_name_key("arvane:stone_stairs"),
      "block.arvane.stone_stairs.display_name");
  EXPECT_EQ(
      arv::text::item_display_name_key("example:tools/hammer"),
      "item.example.tools.hammer.display_name");
}
