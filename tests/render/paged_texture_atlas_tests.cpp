#include "arv/render/paged_texture_atlas.hpp"

#include <gtest/gtest.h>

TEST(PagedTextureAtlasTests, AllocationsRecordPageIndex) {
  arv::render::PagedTextureAtlas atlas({
      4,
      4,
      2,
      arv::render::TextureFormat::Rgba8,
      false,
      "test"});

  const auto first = atlas.allocate(4, 4);
  const auto second = atlas.allocate(4, 4);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  const auto first_region = atlas.resolve(first.value());
  const auto second_region = atlas.resolve(second.value());
  ASSERT_TRUE(first_region.has_value());
  ASSERT_TRUE(second_region.has_value());
  EXPECT_EQ(first_region->page_index, 0U);
  EXPECT_EQ(second_region->page_index, 1U);
}

TEST(PagedTextureAtlasTests, FillsFirstPageBeforeSpilling) {
  arv::render::PagedTextureAtlas atlas({
      4,
      4,
      2,
      arv::render::TextureFormat::Rgba8,
      false,
      "test"});

  const auto first = atlas.allocate(2, 4);
  const auto second = atlas.allocate(2, 4);
  const auto third = atlas.allocate(2, 4);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(atlas.resolve(first.value())->page_index, 0U);
  EXPECT_EQ(atlas.resolve(second.value())->page_index, 0U);
  EXPECT_EQ(atlas.resolve(third.value())->page_index, 1U);
}

TEST(PagedTextureAtlasTests, AllocationFailsWhenAllPagesAreFull) {
  arv::render::PagedTextureAtlas atlas({
      4,
      4,
      1,
      arv::render::TextureFormat::Rgba8,
      false,
      "test"});

  ASSERT_TRUE(atlas.allocate(4, 4).has_value());
  EXPECT_FALSE(atlas.allocate(1, 1).has_value());
}

TEST(PagedTextureAtlasTests, FreeAllowsReuseAndInvalidatesOldHandle) {
  arv::render::PagedTextureAtlas atlas({
      4,
      4,
      1,
      arv::render::TextureFormat::Rgba8,
      false,
      "test"});

  const auto first = atlas.allocate(4, 4);
  ASSERT_TRUE(first.has_value());
  atlas.free(first.value());

  EXPECT_FALSE(atlas.resolve(first.value()).has_value());

  const auto second = atlas.allocate(4, 4);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->id, first->id);
  EXPECT_NE(second->generation, first->generation);
  EXPECT_TRUE(atlas.resolve(second.value()).has_value());
}
