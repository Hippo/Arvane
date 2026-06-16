#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/item.hpp"
#include "arv/render/entity_model.hpp"
#include "arv/voxel/block.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

[[nodiscard]] arv::voxel::BlockRegistry make_chest_block_registry() {
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
  EXPECT_TRUE(definitions) << definitions.error().message;
  auto registry = arv::voxel::BlockRegistry::build(std::move(definitions.value()));
  EXPECT_TRUE(registry) << registry.error().message;
  return std::move(registry.value());
}

[[nodiscard]] const arv::render::EntityModelAsset* find_model(
    const arv::client::EntityRenderAssets& assets,
    arv::render::EntityModelID id) noexcept {
  for (const arv::render::EntityModelAsset& model : assets.models) {
    if (model.id == id) {
      return &model;
    }
  }
  return nullptr;
}

} // namespace

TEST(EntityRenderAssetsTests, BlockAttachedModelsUseEntityModelBaker) {
  arv::voxel::BlockRegistry registry = make_chest_block_registry();
  const std::optional<arv::voxel::BlockVariantID> chest_variant =
      registry.find_variant("arvane:chest[facing=north]");
  ASSERT_TRUE(chest_variant.has_value());
  const std::optional<arv::voxel::BlockVariantID> east_chest_variant =
      registry.find_variant("arvane:chest[facing=east]");
  ASSERT_TRUE(east_chest_variant.has_value());

  arv::asset::ResourceManager resources(ARVANE_TEST_ASSET_ROOT);
  auto block_models = arv::asset::load_block_model_definitions(resources, registry);
  ASSERT_TRUE(block_models) << block_models.error().message;
  arv::asset::apply_block_model_collision_boxes(registry, block_models.value());
  auto block_atlas = arv::asset::build_block_texture_atlas(
      resources,
      registry,
      block_models.value());
  ASSERT_TRUE(block_atlas) << block_atlas.error().message;

  auto entity_registry = arv::entity::EntityRegistry::build({});
  ASSERT_TRUE(entity_registry) << entity_registry.error().message;
  auto item_registry = arv::player::ItemRegistry::build_from_blocks_and_items(registry, {});
  ASSERT_TRUE(item_registry) << item_registry.error().message;

  auto assets = arv::client::build_entity_render_assets(
      resources,
      entity_registry.value(),
      item_registry.value(),
      registry,
      block_atlas.value().render_data,
      std::span<const arv::asset::BlockModelAssetData>{
          block_models.value().data(),
          block_models.value().size()});
  ASSERT_TRUE(assets) << assets.error().message;
  ASSERT_LT(chest_variant->value, assets.value().block_attached_model_by_variant.size());
  ASSERT_LT(east_chest_variant->value, assets.value().block_attached_rotation_by_variant.size());
  const arv::render::EntityModelID model_id =
      assets.value().block_attached_model_by_variant[chest_variant->value];
  ASSERT_NE(model_id.value, 0U);
  EXPECT_EQ(
      assets.value().block_attached_model_by_variant[east_chest_variant->value].value,
      7'000'000U + east_chest_variant->value);
  EXPECT_FLOAT_EQ(
      assets.value().block_attached_rotation_by_variant[chest_variant->value].w,
      1.0F);
  EXPECT_FLOAT_EQ(
      assets.value().block_attached_rotation_by_variant[east_chest_variant->value].y,
      -0.70710677F);

  const arv::render::EntityModelAsset* built_model = find_model(assets.value(), model_id);
  ASSERT_NE(built_model, nullptr);
  ASSERT_EQ(built_model->bones.size(), 2U);
  EXPECT_EQ(built_model->bones[0].name, "base");
  EXPECT_EQ(built_model->bones[1].name, "lid");

  auto model_text = resources.load_text({"arvane:model/entity/chest"}, arv::asset::AssetKind::Model);
  ASSERT_TRUE(model_text) << model_text.error().message;
  auto definition = arv::render::parse_entity_model_json(model_text.value());
  ASSERT_TRUE(definition) << definition.error().message;
  if (definition.value().name.empty()) {
    definition.value().name = "chest";
  }

  const arv::asset::TextureAtlasEntry* entry =
      assets.value().texture_atlas.find("arvane:texture/entity/chest");
  ASSERT_NE(entry, nullptr);
  const std::vector<arv::render::EntityModelTextureBinding> bindings{{
      "arvane:texture/entity/chest",
      entry->unpadded_pixel_min,
      entry->unpadded_pixel_max,
      entry->width,
      entry->height,
      arv::render::TextureAtlasDomain::Entities,
      static_cast<arv::u16>(entry->page),
  }};
  auto expected = arv::render::build_entity_model_asset(
      model_id,
      definition.value(),
      bindings);
  ASSERT_TRUE(expected) << expected.error().message;

  ASSERT_EQ(built_model->vertices.size(), expected.value().vertices.size());
  ASSERT_EQ(built_model->indices, expected.value().indices);
  for (arv::usize index = 0; index < built_model->vertices.size(); ++index) {
    const arv::render::EntityModelMeshVertex& actual = built_model->vertices[index];
    const arv::render::EntityModelMeshVertex& wanted = expected.value().vertices[index];
    EXPECT_EQ(actual.position, wanted.position);
    EXPECT_EQ(actual.uv, wanted.uv);
    EXPECT_EQ(actual.bone_index, wanted.bone_index);
    EXPECT_EQ(actual.atlas_page, wanted.atlas_page);
    EXPECT_EQ(actual.texture_domain, wanted.texture_domain);
  }
}
