#pragma once

#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/player/container.hpp"
#include "arv/render/chunk_mesher.hpp"
#include "arv/voxel/block.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace arv::client {

struct ClientBlockRenderAssets {
  voxel::BlockRegistry block_registry{};
  player::ContainerTypeRegistry container_types{};
  player::ContainerUiLayoutRegistry container_layouts{};
  std::vector<asset::BlockModelAssetData> block_models{};
  asset::BlockTextureAtlas block_atlas{};
  render::FluidRenderData fluid_render_data{};
};

struct ClientBlockRenderAssetLoadOptions {
  FunctionRef<bool(std::string_view)> block_behavior_exists{};
  asset::TextureAtlasBuildOptions atlas_options{
      asset::default_block_texture_atlas_options()};
};

struct ClientBlockRenderAssetLoadError {
  std::string category{};
  std::string message{};
};

using ClientBlockRenderAssetsResult =
    Expected<ClientBlockRenderAssets, ClientBlockRenderAssetLoadError>;

[[nodiscard]] ClientBlockRenderAssetsResult load_client_block_render_assets(
    asset::PackManager& packs,
    asset::ResourceManager& resources,
    ClientBlockRenderAssetLoadOptions options = {});

} // namespace arv::client
