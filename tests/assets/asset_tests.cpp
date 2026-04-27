#include "arv/asset/font.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"

#include <filesystem>
#include <array>

#include <gtest/gtest.h>

namespace {

arv::asset::Image make_test_image(std::array<arv::u8, 4> a, std::array<arv::u8, 4> b,
                                  std::array<arv::u8, 4> c, std::array<arv::u8, 4> d) {
  arv::asset::Image image;
  image.width = 2;
  image.height = 2;
  image.channels = 4;
  image.pixels = {
      a[0], a[1], a[2], a[3],
      b[0], b[1], b[2], b[3],
      c[0], c[1], c[2], c[3],
      d[0], d[1], d[2], d[3],
  };
  return image;
}

std::array<arv::u8, 4> pixel_at(const arv::asset::Image& image, arv::u32 x, arv::u32 y) {
  const arv::usize offset = (static_cast<arv::usize>(y) * image.width + x) * 4U;
  return {{
      image.pixels[offset + 0U],
      image.pixels[offset + 1U],
      image.pixels[offset + 2U],
      image.pixels[offset + 3U],
  }};
}

} // namespace

TEST(AssetResourceTests, ResolvesNamespacedTexturePaths) {
  arv::asset::ResourceManager resources("/tmp/arvane_assets");
  EXPECT_TRUE(arv::asset::is_valid_asset_id("arvane:texture/block/stone"));
  EXPECT_FALSE(arv::asset::is_valid_asset_id("arvane:texture/../stone"));

  const std::filesystem::path resolved =
      resources.resolve_asset_path({"arvane:texture/block/stone"}, arv::asset::AssetKind::Texture);
  EXPECT_EQ(resolved, std::filesystem::path{"/tmp/arvane_assets/textures/block/stone.png"});
}

TEST(AssetResourceTests, MissingTextureFallbackIsCachedByIdentity) {
  arv::asset::ResourceManager resources("/tmp/arvane_assets");

  auto first = resources.load_texture({"arvane:texture/block/not_present"});
  ASSERT_TRUE(first);
  auto second = resources.load_texture({"arvane:texture/block/not_present"});
  ASSERT_TRUE(second);

  EXPECT_EQ(first.value(), second.value());
  const arv::asset::TextureResource* texture = resources.texture(first.value());
  ASSERT_NE(texture, nullptr);
  EXPECT_TRUE(texture->fallback);
  EXPECT_EQ(texture->image.width, 16U);
}

TEST(TextureAtlasTests, AppliesDuplicatedEdgePadding) {
  const arv::asset::Image image = make_test_image(
      {1, 2, 3, 255},
      {4, 5, 6, 255},
      {7, 8, 9, 255},
      {10, 11, 12, 255});
  const arv::asset::TextureAtlasInput input{{"arvane:texture/block/test"}, image, false};
  auto atlas = arv::asset::build_texture_atlas(std::span<const arv::asset::TextureAtlasInput>{&input, 1}, {.max_page_size = 16, .padding = 1});
  ASSERT_TRUE(atlas);
  ASSERT_EQ(atlas.value().pages.size(), 1U);
  ASSERT_EQ(atlas.value().entries.size(), 1U);

  const arv::asset::TextureAtlasEntry& entry = atlas.value().entries.front();
  const arv::asset::Image& page = atlas.value().pages.front().image;
  EXPECT_EQ(pixel_at(page, static_cast<arv::u32>(entry.padded_pixel_min.x), static_cast<arv::u32>(entry.padded_pixel_min.y)),
            pixel_at(image, 0, 0));
  EXPECT_EQ(pixel_at(page, static_cast<arv::u32>(entry.padded_pixel_max.x - 1), static_cast<arv::u32>(entry.padded_pixel_max.y - 1)),
            pixel_at(image, 1, 1));
  EXPECT_EQ(entry.unpadded_pixel_min.x, entry.padded_pixel_min.x + 1);
  EXPECT_EQ(entry.unpadded_pixel_min.y, entry.padded_pixel_min.y + 1);
}

TEST(FontTests, ShapesMeasuresAndCachesGlyphs) {
  const std::filesystem::path asset_root{ARVANE_TEST_ASSET_ROOT};
  arv::asset::ResourceManager resources(asset_root);
  arv::asset::FontSystem fonts;
  ASSERT_TRUE(fonts.initialized());

  auto primary = fonts.load_face(
      resources.resolve_asset_path({"arvane:font/font"}, arv::asset::AssetKind::Font),
      18,
      {"arvane:font/font"});
  ASSERT_TRUE(primary) << primary.error().message;

  const arv::asset::FontFaceHandle handles[] = {primary.value()};
  const arv::asset::FontStack stack = fonts.make_stack(handles);
  auto measured = fonts.measure_text("Hotbar 1", stack);
  ASSERT_TRUE(measured) << measured.error().message;
  EXPECT_GT(measured.value().width, 0.0F);
  EXPECT_GT(measured.value().line_height, 0.0F);

  EXPECT_EQ(fonts.cached_glyph_count(primary.value()), 0U);
  auto layout = fonts.layout_text("Hotbar 1", stack, {10.0F, 20.0F}, {1.0F, 1.0F, 1.0F, 1.0F});
  ASSERT_TRUE(layout) << layout.error().message;
  EXPECT_FALSE(layout.value().quads.empty());
  EXPECT_GT(fonts.cached_glyph_count(primary.value()), 0U);
  EXPECT_TRUE(fonts.atlas_dirty());
}
