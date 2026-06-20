#pragma once

#include "arv/asset/texture_atlas.hpp"
#include "arv/client/client_ui_assets.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/item_animation.hpp"

#include <span>
#include <string>

namespace arv::client {

enum class ClientGameplayAssetLoadStage : u8 {
  EntityRenderAssets,
  ItemAnimations,
  UiTextureAssets,
};

struct ClientGameplayAssetLoadError {
  ClientGameplayAssetLoadStage stage{ClientGameplayAssetLoadStage::EntityRenderAssets};
  std::string category{};
  std::string operation{};
  std::string message{};
};

struct ClientGameplayAssetLoadOptions {
  ClientEntityRenderAssetLoadOptions entity_assets{};
  asset::TextureAtlasBuildOptions ui_texture_atlas_options{
      .max_page_size = 1024,
      .max_pages = 1,
      .padding = 2,
      .require_uniform_non_fallback_size = false};
};

struct ClientGameplayAssets {
  ClientEntityRenderAssets entity_assets{};
  ItemAnimationLibrary item_animation_library{};
  ClientUiTextureAssets ui_texture_assets{};
};

using ClientGameplayAssetsResult =
    Expected<ClientGameplayAssets, ClientGameplayAssetLoadError>;

[[nodiscard]] ClientGameplayAssetsResult load_client_gameplay_assets(
    const asset::PackManager& packs,
    asset::ResourceManager& resources,
    const voxel::BlockRegistry& block_registry,
    const voxel::BlockRenderData& block_render_data,
    std::span<const asset::BlockModelAssetData> block_models,
    ClientGameplayAssetLoadOptions options = {});

} // namespace arv::client
