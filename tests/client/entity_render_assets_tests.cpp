#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/base/base_module.hpp"
#include "arv/client/block_render_assets.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/item.hpp"
#include "arv/render/entity_model.hpp"
#include "arv/voxel/block.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <utility>
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
      "path": "arvane:model/entity/chest",
      "animation": "arvane:animation/chest"
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

[[nodiscard]] std::filesystem::path test_arvane_home() {
  std::filesystem::path path{ARVANE_TEST_ASSET_ROOT};
  return path.parent_path().parent_path().parent_path().parent_path();
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

[[nodiscard]] const arv::render::EntityAnimationAsset* find_animation(
    const arv::client::EntityRenderAssets& assets,
    arv::render::EntityAnimationID id) noexcept {
  for (const arv::render::EntityAnimationAsset& animation : assets.animations) {
    if (animation.id == id) {
      return &animation;
    }
  }
  return nullptr;
}

[[nodiscard]] std::optional<std::pair<arv::f32, arv::f32>> model_y_bounds(
    const arv::render::EntityModelAsset& model) noexcept {
  if (model.vertices.empty()) {
    return std::nullopt;
  }
  arv::f32 min_y = std::numeric_limits<arv::f32>::max();
  arv::f32 max_y = std::numeric_limits<arv::f32>::lowest();
  for (const arv::render::EntityModelMeshVertex& vertex : model.vertices) {
    min_y = std::min(min_y, vertex.position.y);
    max_y = std::max(max_y, vertex.position.y);
  }
  return std::pair{min_y, max_y};
}

} // namespace

TEST(EntityRenderAssetsTests, ClientEntityLoaderUsesCustomEntityRegistryBuilder) {
  auto packs = arv::asset::PackManager::load(test_arvane_home());
  ASSERT_TRUE(packs) << packs.error().describe();
  arv::asset::ResourceManager resources(packs.value());

  auto block_assets =
      arv::client::load_client_block_render_assets(packs.value(), resources);
  ASSERT_TRUE(block_assets) << block_assets.error().message;

  int builder_calls = 0;
  const auto build_entity_registry =
      [&builder_calls](
          std::vector<arv::entity::EntityDefinition> definitions)
      -> arv::entity::EntityRegistry::Result {
    ++builder_calls;
    return arv::base::build_base_entity_registry(
        std::move(definitions),
        nullptr,
        false);
  };
  arv::client::ClientEntityRenderAssetLoadOptions options;
  options.build_entity_registry =
      arv::FunctionRef<arv::entity::EntityRegistry::Result(
          std::vector<arv::entity::EntityDefinition>)>{build_entity_registry};

  auto entity_assets = arv::client::load_client_entity_render_assets(
      packs.value(),
      resources,
      block_assets.value().block_registry,
      block_assets.value().block_atlas.render_data,
      block_assets.value().block_models,
      options);

  ASSERT_TRUE(entity_assets) << entity_assets.error().message;
  EXPECT_EQ(builder_calls, 1);
  const std::optional<arv::EntityTypeID> player_type =
      entity_assets.value().entity_registry.find_type(
          arv::entity::PlayerEntityTypeName);
  ASSERT_TRUE(player_type.has_value());
  EXPECT_EQ(player_type.value(), arv::entity::PlayerEntityTypeID);
  EXPECT_FALSE(entity_assets.value().render_assets.models.empty());
  EXPECT_FALSE(entity_assets.value().vehicle_registry.records().empty());
}

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
  ASSERT_LT(chest_variant->value, assets.value().block_attached_animation_by_variant.size());
  ASSERT_LT(east_chest_variant->value, assets.value().block_attached_rotation_by_variant.size());
  const arv::render::EntityModelID model_id =
      assets.value().block_attached_model_by_variant[chest_variant->value];
  const arv::render::EntityAnimationID animation_id =
      assets.value().block_attached_animation_by_variant[chest_variant->value];
  ASSERT_NE(model_id.value, 0U);
  ASSERT_NE(animation_id.value, 0U);
  EXPECT_EQ(
      assets.value().block_attached_model_by_variant[east_chest_variant->value].value,
      7'000'000U + east_chest_variant->value);
  EXPECT_EQ(
      assets.value().block_attached_animation_by_variant[east_chest_variant->value].value,
      8'000'000U + east_chest_variant->value);
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
  const std::optional<std::pair<arv::f32, arv::f32>> attached_bounds =
      model_y_bounds(*built_model);
  ASSERT_TRUE(attached_bounds.has_value());
  EXPECT_GT(attached_bounds->first, -0.01F);
  EXPECT_GT(attached_bounds->second, 14.9F);
  const arv::render::EntityAnimationAsset* built_animation =
      find_animation(assets.value(), animation_id);
  ASSERT_NE(built_animation, nullptr);
  ASSERT_EQ(built_animation->clips.size(), 2U);
  EXPECT_EQ(built_animation->clips[0].name, "open");
  EXPECT_EQ(built_animation->clips[1].name, "close");

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

  const std::optional<arv::player::ItemID> chest_item =
      item_registry.value().find_item("arvane:chest");
  ASSERT_TRUE(chest_item.has_value());
  const auto item_model_id =
      assets.value().model_by_item.find(chest_item.value());
  ASSERT_NE(item_model_id, assets.value().model_by_item.end());
  const arv::render::EntityModelAsset* item_model =
      find_model(assets.value(), item_model_id->second);
  ASSERT_NE(item_model, nullptr);
  const std::optional<std::pair<arv::f32, arv::f32>> item_bounds =
      model_y_bounds(*item_model);
  ASSERT_TRUE(item_bounds.has_value());
  EXPECT_NEAR(item_bounds->first, -8.0F, 0.01F);
  EXPECT_LE(item_bounds->second, 8.0F);
  EXPECT_GT(item_bounds->second, 6.9F);
}
