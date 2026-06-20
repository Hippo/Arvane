#include "arv/render/paged_texture_region_allocator.hpp"

#include <gtest/gtest.h>

TEST(PagedTextureRegionAllocatorTests, PacksMultipleRegionsOntoOnePage) {
  arv::render::PagedTextureRegionAllocator allocator;
  allocator.configure({128, 64, 1});

  const auto first = allocator.allocate(64, 64);
  const auto second = allocator.allocate(64, 64);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  const auto first_region = allocator.resolve(first.value());
  const auto second_region = allocator.resolve(second.value());
  ASSERT_TRUE(first_region.has_value());
  ASSERT_TRUE(second_region.has_value());
  EXPECT_EQ(first_region->page_index, 0U);
  EXPECT_EQ(second_region->page_index, 0U);
  EXPECT_NE(first_region->x, second_region->x);
}

TEST(PagedTextureRegionAllocatorTests, SpillsOnlyAfterCurrentPagesAreFull) {
  arv::render::PagedTextureRegionAllocator allocator;
  allocator.configure({64, 64, 2});

  const auto first = allocator.allocate(64, 64);
  const auto second = allocator.allocate(64, 64);

  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(allocator.resolve(first.value())->page_index, 0U);
  EXPECT_EQ(allocator.resolve(second.value())->page_index, 1U);
}

TEST(PagedTextureRegionAllocatorTests, RejectsOversizedRegionsAndFullAtlas) {
  arv::render::PagedTextureRegionAllocator allocator;
  allocator.configure({64, 64, 1});

  EXPECT_FALSE(allocator.allocate(128, 64).has_value());
  ASSERT_TRUE(allocator.allocate(64, 64).has_value());
  EXPECT_FALSE(allocator.allocate(1, 1).has_value());
}

TEST(PagedTextureRegionAllocatorTests, ClearInvalidatesPreviousHandles) {
  arv::render::PagedTextureRegionAllocator allocator;
  allocator.configure({64, 64, 1});

  const auto handle = allocator.allocate(64, 64);
  ASSERT_TRUE(handle.has_value());
  allocator.clear();

  EXPECT_FALSE(allocator.resolve(handle.value()).has_value());
}

TEST(PagedTextureRegionAllocatorTests, FreeAllowsReuseAndInvalidatesOldHandle) {
  arv::render::PagedTextureRegionAllocator allocator;
  allocator.configure({64, 64, 1});

  const auto first = allocator.allocate(64, 64);
  ASSERT_TRUE(first.has_value());
  allocator.free(first.value());

  EXPECT_FALSE(allocator.resolve(first.value()).has_value());

  const auto second = allocator.allocate(64, 64);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->id, first->id);
  EXPECT_NE(second->generation, first->generation);
  EXPECT_TRUE(allocator.resolve(second.value()).has_value());
}
