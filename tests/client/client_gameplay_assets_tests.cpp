#include "arv/asset/pack.hpp"
#include "arv/asset/pack_bundle.hpp"
#include "arv/asset/pack_bundle_cache.hpp"
#include "arv/asset/resource.hpp"
#include "arv/base/base_module.hpp"
#include "arv/client/block_render_assets.hpp"
#include "arv/client/client_gameplay_assets.hpp"
#include "arv/client/particles.hpp"
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
  EXPECT_FALSE(gameplay_assets.value().entity_assets.vehicle_registry.records().empty());
  EXPECT_GT(gameplay_assets.value().item_animation_library.clip_count(), 0U);
  EXPECT_FALSE(gameplay_assets.value().ui_texture_assets.item_atlas.pages.empty());
  EXPECT_TRUE(gameplay_assets.value().ui_texture_assets.crosshair_image.valid);
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
