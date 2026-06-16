#include "arv/asset/font.hpp"
#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <span>
#include <utility>

#include <gtest/gtest.h>

namespace {

arv::asset::Image make_test_image(std::array<arv::u8, 4> a, std::array<arv::u8, 4> b,
                                  std::array<arv::u8, 4> c, std::array<arv::u8, 4> d) {
  arv::asset::Image image;
  image.width = 2;
  image.height = 2;
  image.channels = 4;
  image.pixels = {
      a[0],
      a[1],
      a[2],
      a[3],
      b[0],
      b[1],
      b[2],
      b[3],
      c[0],
      c[1],
      c[2],
      c[3],
      d[0],
      d[1],
      d[2],
      d[3],
  };
  return image;
}

arv::asset::Image make_solid_image(
    arv::u32 width,
    arv::u32 height,
    std::array<arv::u8, 4> color) {
  arv::asset::Image image;
  image.width = width;
  image.height = height;
  image.channels = 4;
  image.pixels.resize(static_cast<arv::usize>(width) * height * 4U);
  for (arv::usize offset = 0; offset < image.pixels.size(); offset += 4U) {
    image.pixels[offset + 0U] = color[0];
    image.pixels[offset + 1U] = color[1];
    image.pixels[offset + 2U] = color[2];
    image.pixels[offset + 3U] = color[3];
  }
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

  const std::filesystem::path animation =
      resources.resolve_asset_path({"arvane:animation/pig"}, arv::asset::AssetKind::Animation);
  EXPECT_EQ(animation, std::filesystem::path{"/tmp/arvane_assets/animations/pig.json"});
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

TEST(TextureAtlasTests, RecordsPaddingSafeMipmapLimit) {
  const arv::asset::Image image = make_test_image(
      {1, 2, 3, 255},
      {4, 5, 6, 255},
      {7, 8, 9, 255},
      {10, 11, 12, 255});
  const arv::asset::TextureAtlasInput input{{"arvane:texture/block/test"}, image, false};

  auto atlas = arv::asset::build_texture_atlas(
      std::span<const arv::asset::TextureAtlasInput>{&input, 1},
      {.max_page_size = 32, .padding = 4});

  ASSERT_TRUE(atlas);
  EXPECT_EQ(atlas.value().padding, 4U);
  EXPECT_EQ(atlas.value().mip_safe_max_level, 2U);
}

TEST(TextureAtlasTests, SpillsAcrossConfiguredPages) {
  const std::array<arv::asset::TextureAtlasInput, 2> inputs{{
      {{"arvane:texture/block/one"}, make_solid_image(4, 4, {1, 2, 3, 255}), false},
      {{"arvane:texture/block/two"}, make_solid_image(4, 4, {4, 5, 6, 255}), false},
  }};

  auto atlas = arv::asset::build_texture_atlas(
      inputs,
      {.max_page_size = 4, .max_pages = 2, .padding = 0});

  ASSERT_TRUE(atlas);
  ASSERT_EQ(atlas.value().pages.size(), 2U);
  ASSERT_NE(atlas.value().find("arvane:texture/block/one"), nullptr);
  ASSERT_NE(atlas.value().find("arvane:texture/block/two"), nullptr);
  EXPECT_EQ(atlas.value().find("arvane:texture/block/one")->page, 0U);
  EXPECT_EQ(atlas.value().find("arvane:texture/block/two")->page, 1U);
}

TEST(TextureAtlasTests, FailsWhenConfiguredPagesAreFull) {
  const std::array<arv::asset::TextureAtlasInput, 2> inputs{{
      {{"arvane:texture/block/one"}, make_solid_image(4, 4, {1, 2, 3, 255}), false},
      {{"arvane:texture/block/two"}, make_solid_image(4, 4, {4, 5, 6, 255}), false},
  }};

  auto atlas = arv::asset::build_texture_atlas(
      inputs,
      {.max_page_size = 4, .max_pages = 1, .padding = 0});

  EXPECT_FALSE(atlas);
}

TEST(TextureAtlasTests, BuildsBlockAtlasWithMixedTextureSizesByDefault) {
  auto definitions = arv::voxel::parse_block_definitions_json(R"json(
[
  {
    "name": "arvane:air",
    "render_category": "none",
    "properties": { "replaceable": true }
  },
  {
    "name": "arvane:lowres_block",
    "render_category": "opaque",
    "textures": "arvane:texture/block/cobblestone",
    "properties": {
      "solid": true,
      "collidable": true,
      "opaque": true,
      "blocks_light": true,
      "full_cube": true,
      "ao_occluder": true
    }
  },
  {
    "name": "arvane:hires_block",
    "render_category": "opaque",
    "textures": "arvane:texture/block/water",
    "properties": {
      "solid": true,
      "collidable": true,
      "opaque": true,
      "blocks_light": true,
      "full_cube": true,
      "ao_occluder": true
    }
  }
]
)json");
  ASSERT_TRUE(definitions) << definitions.error().message;
  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;

  arv::asset::ResourceManager resources(ARVANE_TEST_ASSET_ROOT);
  auto atlas_result =
      arv::asset::build_block_texture_atlas(resources, registry_result.value());

  ASSERT_TRUE(atlas_result) << atlas_result.error().message;
  const arv::asset::TextureAtlasEntry* lowres_entry =
      atlas_result.value().atlas.find("arvane:texture/block/cobblestone");
  const arv::asset::TextureAtlasEntry* hires_entry =
      atlas_result.value().atlas.find("arvane:texture/block/water");
  ASSERT_NE(lowres_entry, nullptr);
  ASSERT_NE(hires_entry, nullptr);
  EXPECT_EQ(lowres_entry->width, 16U);
  EXPECT_EQ(lowres_entry->height, 16U);
  EXPECT_EQ(hires_entry->width, 32U);
  EXPECT_EQ(hires_entry->height, 32U);

  const auto lowres_variant =
      registry_result.value().find_variant("arvane:lowres_block");
  const auto hires_variant =
      registry_result.value().find_variant("arvane:hires_block");
  ASSERT_TRUE(lowres_variant.has_value());
  ASSERT_TRUE(hires_variant.has_value());

  const arv::voxel::BlockFaceRenderData* lowres_face =
      atlas_result.value().render_data.face(
          lowres_variant.value(),
          arv::voxel::BlockFace::PositiveY);
  const arv::voxel::BlockFaceRenderData* hires_face =
      atlas_result.value().render_data.face(
          hires_variant.value(),
          arv::voxel::BlockFace::PositiveY);
  ASSERT_NE(lowres_face, nullptr);
  ASSERT_NE(hires_face, nullptr);

  const auto uv_extent = [](const std::array<arv::Vec2i, 4>& uvs) {
    arv::i32 min_x = uvs.front().x;
    arv::i32 max_x = uvs.front().x;
    arv::i32 min_y = uvs.front().y;
    arv::i32 max_y = uvs.front().y;
    for (const arv::Vec2i uv : uvs) {
      min_x = std::min(min_x, uv.x);
      max_x = std::max(max_x, uv.x);
      min_y = std::min(min_y, uv.y);
      max_y = std::max(max_y, uv.y);
    }
    return arv::Vec2i{max_x - min_x, max_y - min_y};
  };
  EXPECT_EQ(uv_extent(lowres_face->uvs), (arv::Vec2i{16, 16}));
  EXPECT_EQ(uv_extent(hires_face->uvs), (arv::Vec2i{32, 32}));
}

TEST(TextureAtlasTests, BuildsBlockAtlasRenderDataForInputTexturedModels) {
  auto definitions = arv::voxel::parse_block_definitions_json(R"json(
[
  {
    "name": "arvane:air",
    "render_category": "none",
    "properties": { "replaceable": true }
  },
  {
    "name": "arvane:stone_stairs",
    "render_category": "opaque",
    "model": {
      "path": "arvane:model/block/stairs",
      "textures": ["arvane:texture/block/cobblestone"]
    },
    "properties": {
      "solid": true,
      "collidable": true,
      "opaque": true,
      "blocks_light": true,
      "ao_occluder": true
    }
  }
]
)json");
  ASSERT_TRUE(definitions) << definitions.error().message;
  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;

  arv::asset::ResourceManager resources(ARVANE_TEST_ASSET_ROOT);
  auto atlas_result =
      arv::asset::build_block_texture_atlas(resources, registry_result.value());

  ASSERT_TRUE(atlas_result) << atlas_result.error().message;
  EXPECT_NE(
      atlas_result.value().atlas.find("arvane:texture/block/cobblestone"),
      nullptr);

  const auto stairs_variant =
      registry_result.value().find_variant("arvane:stone_stairs");
  ASSERT_TRUE(stairs_variant.has_value());
  const std::span<const arv::voxel::BlockModelBoxRenderData> boxes =
      atlas_result.value().render_data.model_boxes(stairs_variant.value());
  ASSERT_EQ(boxes.size(), 2U);
  EXPECT_TRUE(
      boxes.front()
          .faces[arv::voxel::face_index(arv::voxel::BlockFace::PositiveY)]
          .has_value());
}

TEST(TextureAtlasTests, CachesStateTransformedModelBoxesWithoutRotatingUvs) {
  auto definitions = arv::voxel::parse_block_definitions_json(R"json(
[
  {
    "name": "arvane:air",
    "render_category": "none",
    "properties": { "replaceable": true }
  },
  {
    "name": "arvane:stone_stairs",
    "states": {
      "facing": ["north", "south", "east", "west"],
      "half": ["bottom", "top"]
    },
    "render_category": "opaque",
    "model": {
      "path": "arvane:model/block/stairs",
      "textures": ["arvane:texture/block/cobblestone"]
    },
    "properties": {
      "solid": true,
      "collidable": true,
      "opaque": true,
      "blocks_light": true,
      "ao_occluder": true
    }
  }
]
)json");
  ASSERT_TRUE(definitions) << definitions.error().message;
  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;

  arv::asset::ResourceManager resources(ARVANE_TEST_ASSET_ROOT);
  auto atlas_result =
      arv::asset::build_block_texture_atlas(resources, registry_result.value());

  ASSERT_TRUE(atlas_result) << atlas_result.error().message;
  const auto stairs_variant =
      registry_result.value().find_variant("arvane:stone_stairs[facing=east,half=bottom]");
  ASSERT_TRUE(stairs_variant.has_value());
  const std::span<const arv::voxel::BlockModelBoxRenderData> boxes =
      atlas_result.value().render_data.model_boxes(stairs_variant.value());
  ASSERT_EQ(boxes.size(), 2U);

  EXPECT_EQ(boxes.back().min.x, 8);
  EXPECT_EQ(boxes.back().max.x, 16);
  EXPECT_EQ(boxes.back().min.z, 0);
  EXPECT_EQ(boxes.back().max.z, 16);

  const auto& east_face =
      boxes.back().faces[arv::voxel::face_index(arv::voxel::BlockFace::PositiveX)];
  const auto& north_face =
      boxes.back().faces[arv::voxel::face_index(arv::voxel::BlockFace::NegativeZ)];
  ASSERT_TRUE(east_face.has_value());
  ASSERT_TRUE(north_face.has_value());
  const int uv_width =
      east_face->uvs[1].x > east_face->uvs[0].x
          ? east_face->uvs[1].x - east_face->uvs[0].x
          : east_face->uvs[0].x - east_face->uvs[1].x;
  const int north_uv_width =
      north_face->uvs[0].x > north_face->uvs[1].x
          ? north_face->uvs[0].x - north_face->uvs[1].x
          : north_face->uvs[1].x - north_face->uvs[0].x;
  EXPECT_EQ(uv_width, north_uv_width * 2);
}

TEST(TextureAtlasTests, AppliesModelCollisionBoxesPerVariantAtStartup) {
  auto definitions = arv::voxel::parse_block_definitions_json(R"json(
[
  {
    "name": "arvane:air",
    "render_category": "none",
    "properties": { "replaceable": true }
  },
  {
    "name": "arvane:stone_stairs",
    "render_category": "opaque",
    "model": {
      "path": "arvane:model/block/stairs",
      "textures": ["arvane:texture/block/cobblestone"]
    },
    "properties": {
      "solid": true,
      "collidable": true,
      "opaque": true,
      "blocks_light": true,
      "ao_occluder": true
    }
  }
]
)json");
  ASSERT_TRUE(definitions) << definitions.error().message;
  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  arv::voxel::BlockRegistry registry = std::move(registry_result.value());

  const auto stairs_variant = registry.find_variant("arvane:stone_stairs");
  ASSERT_TRUE(stairs_variant.has_value());
  EXPECT_TRUE(registry.collision_boxes(stairs_variant.value()).empty());

  arv::asset::ResourceManager resources(ARVANE_TEST_ASSET_ROOT);
  auto block_models = arv::asset::load_block_model_definitions(resources, registry);
  ASSERT_TRUE(block_models) << block_models.error().message;

  arv::asset::apply_block_model_collision_boxes(registry, block_models.value());

  const std::span<const arv::Aabb3f> boxes =
      registry.collision_boxes(stairs_variant.value());
  ASSERT_EQ(boxes.size(), 2U);
  EXPECT_FLOAT_EQ(boxes.front().min.y, 0.0F);
  EXPECT_FLOAT_EQ(boxes.front().max.y, 0.5F);
  EXPECT_FLOAT_EQ(boxes.back().max.y, 1.0F);
}

TEST(TextureAtlasTests, LoadsBlockAttachedChestModelAndCollisionFromAuthoredModel) {
  auto definitions = arv::voxel::parse_block_definitions_json(R"json(
[
  {
    "name": "arvane:air",
    "render_category": "none",
    "properties": { "replaceable": true }
  },
  {
    "name": "arvane:chest",
    "render_category": "opaque",
    "render_mode": "block_entity_model",
    "container_type": "arvane:chest",
    "model": {
      "path": "arvane:model/entity/chest"
    },
    "states": {
      "facing": ["north", "east"]
    },
    "properties": {
      "solid": true,
      "collidable": true,
      "full_cube": false
    }
  }
]
)json");
  ASSERT_TRUE(definitions) << definitions.error().message;
  auto registry_result = arv::voxel::BlockRegistry::build(std::move(definitions.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  arv::voxel::BlockRegistry registry = std::move(registry_result.value());

  const auto chest_variant = registry.find_variant("arvane:chest[facing=north]");
  ASSERT_TRUE(chest_variant.has_value());

  arv::asset::ResourceManager resources(ARVANE_TEST_ASSET_ROOT);
  auto block_models = arv::asset::load_block_model_definitions(resources, registry);
  ASSERT_TRUE(block_models) << block_models.error().message;

  arv::asset::apply_block_model_collision_boxes(registry, block_models.value());
  auto block_atlas = arv::asset::build_block_texture_atlas(
      resources,
      registry,
      block_models.value());
  ASSERT_TRUE(block_atlas) << block_atlas.error().message;

  const std::span<const arv::Aabb3f> boxes =
      registry.collision_boxes(chest_variant.value());
  ASSERT_EQ(boxes.size(), 3U);
  for (const arv::Aabb3f& box : boxes) {
    EXPECT_GE(box.min.x, 0.0F);
    EXPECT_GE(box.min.y, 0.0F);
    EXPECT_GE(box.min.z, 0.0F);
    EXPECT_LE(box.max.x, 1.0F);
    EXPECT_LE(box.max.y, 1.0F);
    EXPECT_LE(box.max.z, 1.0F);
  }
  EXPECT_FALSE(block_atlas.value().render_data.model_boxes(chest_variant.value()).empty());
}

TEST(TextureAtlasTests, LoadsBlockAttachedChestModelFromPackBlockData) {
  const std::filesystem::path packs_root =
      std::filesystem::path{ARVANE_TEST_ASSET_ROOT}
          .parent_path()
          .parent_path()
          .parent_path();
  auto pack_manager_result =
      arv::asset::PackManager::load_from_packs_root(packs_root);
  ASSERT_TRUE(pack_manager_result) << pack_manager_result.error().describe();
  auto blocks_text =
      pack_manager_result.value().load_merged_json_array_text("blocks", "blocks");
  ASSERT_TRUE(blocks_text) << blocks_text.error().describe();

  auto definitions =
      arv::voxel::parse_block_definitions_json(blocks_text.value());
  ASSERT_TRUE(definitions) << definitions.error().message;
  auto registry_result =
      arv::voxel::BlockRegistry::build(std::move(definitions.value()));
  ASSERT_TRUE(registry_result) << registry_result.error().message;
  arv::voxel::BlockRegistry registry = std::move(registry_result.value());

  const auto chest_variant = registry.find_variant("arvane:chest[facing=north]");
  ASSERT_TRUE(chest_variant.has_value());
  const arv::voxel::BlockVariantRecord* chest =
      registry.variant(chest_variant.value());
  ASSERT_NE(chest, nullptr);
  ASSERT_EQ(chest->render_mode, arv::voxel::BlockRenderMode::BlockEntityModel);

  arv::asset::ResourceManager resources(std::move(pack_manager_result.value()));
  auto block_models = arv::asset::load_block_model_definitions(resources, registry);
  ASSERT_TRUE(block_models) << block_models.error().message;
  arv::asset::apply_block_model_collision_boxes(registry, block_models.value());

  const std::span<const arv::Aabb3f> boxes =
      registry.collision_boxes(chest_variant.value());
  ASSERT_FALSE(boxes.empty());
  EXPECT_GT(boxes.front().max.y, 0.5F);
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
  EXPECT_FLOAT_EQ(layout.value().measure.width, measured.value().width);
  EXPECT_FLOAT_EQ(layout.value().measure.height, measured.value().height);
  EXPECT_GT(fonts.cached_glyph_count(primary.value()), 0U);
  EXPECT_TRUE(fonts.atlas_dirty());
}
