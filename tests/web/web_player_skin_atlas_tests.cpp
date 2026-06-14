#include "web_player_skin_atlas.hpp"

#include <gtest/gtest.h>

TEST(WebPlayerSkinAtlasTests, PacksMultipleSkinsOntoOnePage) {
  arv::web::WebPlayerSkinRegionAllocator atlas;
  atlas.configure({128, 64, 1});

  const auto first = atlas.allocate(64, 64);
  const auto second = atlas.allocate(64, 64);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  const auto first_region = atlas.resolve(first.value());
  const auto second_region = atlas.resolve(second.value());
  ASSERT_TRUE(first_region.has_value());
  ASSERT_TRUE(second_region.has_value());
  EXPECT_EQ(first_region->page_index, 0U);
  EXPECT_EQ(second_region->page_index, 0U);
  EXPECT_NE(first_region->x, second_region->x);
}

TEST(WebPlayerSkinAtlasTests, SpillsOnlyAfterCurrentPagesAreFull) {
  arv::web::WebPlayerSkinRegionAllocator atlas;
  atlas.configure({64, 64, 2});

  const auto first = atlas.allocate(64, 64);
  const auto second = atlas.allocate(64, 64);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(atlas.resolve(first.value())->page_index, 0U);
  EXPECT_EQ(atlas.resolve(second.value())->page_index, 1U);
}

TEST(WebPlayerSkinAtlasTests, RejectsOversizedSkinsAndFullAtlas) {
  arv::web::WebPlayerSkinRegionAllocator atlas;
  atlas.configure({64, 64, 1});

  EXPECT_FALSE(atlas.allocate(128, 64).has_value());
  ASSERT_TRUE(atlas.allocate(64, 64).has_value());
  EXPECT_FALSE(atlas.allocate(1, 1).has_value());
}

TEST(WebPlayerSkinAtlasTests, ClearInvalidatesPreviousHandles) {
  arv::web::WebPlayerSkinRegionAllocator atlas;
  atlas.configure({64, 64, 1});

  const auto handle = atlas.allocate(64, 64);
  ASSERT_TRUE(handle.has_value());
  atlas.clear();

  EXPECT_FALSE(atlas.resolve(handle.value()).has_value());
}
