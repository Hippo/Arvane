#include "arv/asset/pack.hpp"
#include "arv/asset/pack_bundle.hpp"
#include "arv/asset/pack_bundle_cache.hpp"
#include "arv/asset/resource.hpp"
#include "arv/base/base_module.hpp"
#include "arv/client/block_render_assets.hpp"
#include "arv/client/client_gameplay_assets.hpp"
#include "arv/client/particles.hpp"
#include "arv/crafting/crafting.hpp"
#include "arv/entity/components.hpp"
#include "support/temp_dir.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <utility>
#include <vector>

namespace {

[[nodiscard]] std::filesystem::path test_arvane_home() {
  std::filesystem::path path{ARVANE_TEST_ASSET_ROOT};
  return path.parent_path().parent_path().parent_path().parent_path();
}

} // namespace

TEST(ClientGameplayAssetsTests, LoadsEntityItemAnimationAndUiTextureAssetsTogether) {
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
  arv::client::ClientGameplayAssetLoadOptions options;
  options.entity_assets.build_entity_registry =
      arv::FunctionRef<arv::entity::EntityRegistry::Result(
          std::vector<arv::entity::EntityDefinition>)>{build_entity_registry};

  auto gameplay_assets = arv::client::load_client_gameplay_assets(
      packs.value(),
      resources,
      block_assets.value().block_registry,
      block_assets.value().block_atlas.render_data,
      block_assets.value().block_models,
      options);

  ASSERT_TRUE(gameplay_assets) << gameplay_assets.error().message;
  EXPECT_EQ(builder_calls, 1);
  EXPECT_FALSE(gameplay_assets.value().entity_assets.render_assets.models.empty());
  const arv::client::EntityRenderAssets& render_assets =
      gameplay_assets.value().entity_assets.render_assets;
  EXPECT_NE(
      render_assets.space_body_model_by_texture.find("arvane:textures/space_body/overworld"),
      render_assets.space_body_model_by_texture.end());
  EXPECT_NE(
      render_assets.space_body_model_by_texture.find("arvane:textures/space_body/luna"),
      render_assets.space_body_model_by_texture.end());
  const arv::asset::TextureAtlasEntry* overworld_space_texture =
      render_assets.texture_atlas.find("arvane:textures/space_body/overworld");
  ASSERT_NE(overworld_space_texture, nullptr);
  EXPECT_EQ(overworld_space_texture->width, 128U);
  EXPECT_EQ(overworld_space_texture->height, 64U);
  EXPECT_FALSE(overworld_space_texture->fallback);
  EXPECT_FALSE(gameplay_assets.value().entity_assets.vehicle_registry.records().empty());
  EXPECT_GT(gameplay_assets.value().item_animation_library.clip_count(), 0U);
  EXPECT_FALSE(gameplay_assets.value().ui_texture_assets.item_atlas.pages.empty());
  EXPECT_TRUE(gameplay_assets.value().ui_texture_assets.crosshair_image.valid);

  const auto iron_ore_variant =
      block_assets.value().block_registry.find_variant("arvane:iron_ore");
  const auto copper_ore_variant =
      block_assets.value().block_registry.find_variant("arvane:copper_ore");
  const auto furnace_variant =
      block_assets.value().block_registry.find_variant("arvane:furnace");
  ASSERT_TRUE(iron_ore_variant.has_value());
  ASSERT_TRUE(copper_ore_variant.has_value());
  ASSERT_TRUE(furnace_variant.has_value());

  const auto* iron_ore = block_assets.value().block_registry.block(
      block_assets.value().block_registry.block_id(iron_ore_variant.value()));
  const auto* copper_ore = block_assets.value().block_registry.block(
      block_assets.value().block_registry.block_id(copper_ore_variant.value()));
  const auto* furnace =
      block_assets.value().block_registry.variant(furnace_variant.value());
  ASSERT_NE(iron_ore, nullptr);
  ASSERT_NE(copper_ore, nullptr);
  ASSERT_NE(furnace, nullptr);
  EXPECT_EQ(iron_ore->drops.kind, arv::voxel::BlockDropKind::Item);
  EXPECT_EQ(iron_ore->drops.item_name, "arvane:iron");
  EXPECT_EQ(copper_ore->drops.kind, arv::voxel::BlockDropKind::Item);
  EXPECT_EQ(copper_ore->drops.item_name, "arvane:copper");
  EXPECT_EQ(
      furnace->texture_assets[arv::voxel::face_index(arv::voxel::BlockFace::NegativeZ)],
      "arvane:texture/block/furnace_front");
  EXPECT_EQ(
      furnace->texture_assets[arv::voxel::face_index(arv::voxel::BlockFace::PositiveY)],
      "arvane:texture/block/furnace_top");

  const arv::player::ItemRegistry& item_registry =
      gameplay_assets.value().entity_assets.item_registry;
  EXPECT_TRUE(item_registry.find_item("arvane:copper").has_value());
  EXPECT_TRUE(item_registry.find_item("arvane:copper_bar").has_value());
  EXPECT_TRUE(item_registry.find_item("arvane:iron").has_value());
  EXPECT_TRUE(item_registry.find_item("arvane:iron_bar").has_value());
  EXPECT_TRUE(item_registry.find_item("arvane:iron_pickaxe").has_value());
  EXPECT_TRUE(item_registry.find_item("arvane:iron_sword").has_value());
  EXPECT_TRUE(item_registry.find_item("arvane:iron_shovel").has_value());
  EXPECT_FALSE(item_registry.find_item("arvane:wood_pickaxe").has_value());
  EXPECT_FALSE(item_registry.find_item("arvane:wood_sword").has_value());
  EXPECT_FALSE(item_registry.find_item("arvane:wood_shovel").has_value());

  auto crafting_registry =
      arv::crafting::load_crafting_registry_from_packs(packs.value(), item_registry);
  ASSERT_TRUE(crafting_registry) << crafting_registry.error().message;
  EXPECT_TRUE(crafting_registry.value().diagnostics.empty());
  const arv::crafting::CraftingRecipe* furnace_recipe =
      crafting_registry.value().registry.recipe("arvane:furnace");
  ASSERT_NE(furnace_recipe, nullptr);
  EXPECT_EQ(furnace_recipe->result.item_name, "arvane:furnace");
  ASSERT_EQ(furnace_recipe->ingredients.size(), 2U);
  EXPECT_EQ(furnace_recipe->ingredients[0].item_name, "arvane:copper");
  EXPECT_EQ(furnace_recipe->ingredients[0].count, 1U);
  EXPECT_EQ(furnace_recipe->ingredients[1].item_name, "arvane:stone");
  EXPECT_EQ(furnace_recipe->ingredients[1].count, 8U);
}

TEST(ClientGameplayAssetsTests, LoadsGameplayAssetsFromInstalledClientBundleCache) {
  auto source_packs = arv::asset::PackManager::load(test_arvane_home());
  ASSERT_TRUE(source_packs) << source_packs.error().describe();

  auto bundle = arv::asset::build_client_pack_bundle(
      source_packs.value(),
      "{\"format_version\":1,\"blocks\":[],\"block_variants\":[],\"items\":[],\"entities\":[]}");
  ASSERT_TRUE(bundle) << bundle.error().message;

  arv::testsupport::ScopedTempDir temp("arvane_client_bundle_assets_");
  const std::filesystem::path bundle_file = temp.path / "server.apack";
  auto write_bundle =
      arv::asset::write_client_pack_bundle_file(bundle_file, bundle.value());
  ASSERT_TRUE(write_bundle) << write_bundle.error().message;

  const std::filesystem::path cache_root = temp.path / "server-packs";
  auto install = arv::asset::install_client_pack_bundle_from_file(
      bundle_file,
      cache_root,
      bundle.value().bundle_hash);
  ASSERT_TRUE(install) << install.error().message;

  const std::filesystem::path roots[] = {install.value().cache_path};
  auto remote_packs = arv::asset::PackManager::load_client_bundle_roots(roots);
  ASSERT_TRUE(remote_packs) << remote_packs.error().describe();
  arv::asset::ResourceManager resources(remote_packs.value());

  auto block_assets = arv::client::load_client_block_render_assets(
      remote_packs.value(),
      resources);
  ASSERT_TRUE(block_assets) << block_assets.error().category << ": "
                            << block_assets.error().message;
  EXPECT_FALSE(block_assets.value().block_atlas.atlas.pages.empty());

  const std::vector<std::string> block_particle_textures =
      arv::client::block_particle_texture_assets(block_assets.value().block_registry);
  auto particle_assets = arv::client::load_particle_assets_from_packs(
      remote_packs.value(),
      resources,
      arv::client::default_particle_atlas_options(),
      block_particle_textures);
  ASSERT_TRUE(particle_assets) << particle_assets.error().source << ": "
                               << particle_assets.error().message;
  EXPECT_FALSE(particle_assets.value().atlas.pages.empty());

  auto gameplay_assets = arv::client::load_client_gameplay_assets(
      remote_packs.value(),
      resources,
      block_assets.value().block_registry,
      block_assets.value().block_atlas.render_data,
      block_assets.value().block_models);
  ASSERT_TRUE(gameplay_assets) << gameplay_assets.error().category << ": "
                               << gameplay_assets.error().message;
  EXPECT_FALSE(gameplay_assets.value().entity_assets.render_assets.models.empty());
  EXPECT_FALSE(gameplay_assets.value().entity_assets.render_assets.texture_atlas.pages.empty());
  EXPECT_GT(gameplay_assets.value().item_animation_library.clip_count(), 0U);
  EXPECT_FALSE(gameplay_assets.value().ui_texture_assets.item_atlas.pages.empty());
  EXPECT_TRUE(gameplay_assets.value().ui_texture_assets.crosshair_image.valid);
}

TEST(ClientGameplayAssetsTests, ReportsEntityRegistryBuilderFailuresAtEntityStage) {
  auto packs = arv::asset::PackManager::load(test_arvane_home());
  ASSERT_TRUE(packs) << packs.error().describe();
  arv::asset::ResourceManager resources(packs.value());

  auto block_assets =
      arv::client::load_client_block_render_assets(packs.value(), resources);
  ASSERT_TRUE(block_assets) << block_assets.error().message;

  const auto build_entity_registry =
      [](std::vector<arv::entity::EntityDefinition>)
      -> arv::entity::EntityRegistry::Result {
    return arv::entity::EntityRegistry::Result::failure({"builder failed"});
  };
  arv::client::ClientGameplayAssetLoadOptions options;
  options.entity_assets.build_entity_registry =
      arv::FunctionRef<arv::entity::EntityRegistry::Result(
          std::vector<arv::entity::EntityDefinition>)>{build_entity_registry};

  auto gameplay_assets = arv::client::load_client_gameplay_assets(
      packs.value(),
      resources,
      block_assets.value().block_registry,
      block_assets.value().block_atlas.render_data,
      block_assets.value().block_models,
      options);

  ASSERT_FALSE(gameplay_assets);
  EXPECT_EQ(
      gameplay_assets.error().stage,
      arv::client::ClientGameplayAssetLoadStage::EntityRenderAssets);
  EXPECT_EQ(gameplay_assets.error().category, "entities");
  EXPECT_EQ(gameplay_assets.error().operation, "load entity assets");
  EXPECT_EQ(gameplay_assets.error().message, "builder failed");
}
