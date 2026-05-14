#include "arv/text/rich_text.hpp"

#include <gtest/gtest.h>

#include <vector>

TEST(RichTextTests, FlattensInheritedStylesAndPlainText) {
  arv::text::TextStylePatch root_style;
  root_style.bold = true;

  arv::text::TextStylePatch child_style;
  child_style.color = arv::text::RgbColor{0x12U, 0x34U, 0x56U};
  child_style.bold = false;

  arv::text::RichText root = arv::text::plain_rich_text("root ", root_style);
  root.children.push_back(arv::text::plain_rich_text("child", child_style));

  EXPECT_EQ(arv::text::to_plain_text(root), "root child");

  const std::vector<arv::text::RichTextRun> runs =
      arv::text::flatten_rich_text(root);
  ASSERT_EQ(runs.size(), 2U);
  EXPECT_TRUE(runs[0].style.bold);
  EXPECT_FALSE(runs[1].style.bold);
  EXPECT_EQ(runs[1].style.color, (arv::text::RgbColor{0x12U, 0x34U, 0x56U}));
}

TEST(RichTextTests, ValidatesDepthNodeAndTextLimits) {
  arv::text::RichTextLimits limits;
  limits.max_depth = 2U;
  limits.max_nodes = 3U;
  limits.max_text_bytes = 8U;

  arv::text::RichText deep = arv::text::plain_rich_text("a");
  deep.children.push_back(arv::text::plain_rich_text("b"));
  deep.children.front().children.push_back(arv::text::plain_rich_text("c"));
  EXPECT_FALSE(arv::text::validate_rich_text(deep, limits).valid);

  arv::text::RichText many = arv::text::plain_rich_text("a");
  many.children.push_back(arv::text::plain_rich_text("b"));
  many.children.push_back(arv::text::plain_rich_text("c"));
  many.children.push_back(arv::text::plain_rich_text("d"));
  EXPECT_FALSE(arv::text::validate_rich_text(many, limits).valid);

  arv::text::RichText large = arv::text::plain_rich_text("too long for the limit");
  EXPECT_FALSE(arv::text::validate_rich_text(large, limits).valid);
}
