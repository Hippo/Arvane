#include "arv/client/block_render_assets.hpp"

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/base/base_ids.hpp"
#include "arv/base/base_module.hpp"
#include "arv/voxel/fluid.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>

namespace {

[[nodiscard]] std::filesystem::path test_arvane_home() {
  std::filesystem::path path{ARVANE_TEST_ASSET_ROOT};
  return path.parent_path().parent_path().parent_path().parent_path();
}

} // namespace

TEST(ClientBlockRenderAssetsTests, UsesCustomFluidRegistrationCallback) {
  auto packs = arv::asset::PackManager::load(test_arvane_home());
  ASSERT_TRUE(packs) << packs.error().describe();
  arv::asset::ResourceManager resources(packs.value());

  int callback_calls = 0;
  const auto register_fluid_render_data =
      [&callback_calls](
          arv::render::FluidRenderData& render_data,
          const arv::voxel::BlockRegistry& registry)
      -> arv::Expected<void, std::string> {
    ++callback_calls;
    return arv::base::register_base_fluid_render_data(render_data, registry);
  };
  arv::client::ClientBlockRenderAssetLoadOptions options;
  options.register_fluid_render_data =
      arv::FunctionRef<arv::Expected<void, std::string>(
          arv::render::FluidRenderData&,
          const arv::voxel::BlockRegistry&)>{register_fluid_render_data};

  auto assets = arv::client::load_client_block_render_assets(
      packs.value(),
      resources,
      options);

  ASSERT_TRUE(assets) << assets.error().message;
  EXPECT_EQ(callback_calls, 1);
  const std::optional<arv::voxel::BlockVariantID> water_variant =
      assets.value().block_registry.find_variant(arv::base::ids::WaterBlockID);
  ASSERT_TRUE(water_variant.has_value());
  EXPECT_EQ(
      assets.value().fluid_render_data.variant_for(
          arv::voxel::DefaultWaterFluidTypeID),
      water_variant);
}
