#pragma once

#include "arv/asset/texture_atlas.hpp"
#include "arv/client/block_render_assets.hpp"
#include "arv/client/entity_render_assets.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/client/particles.hpp"
#include "arv/client/player_skin_rendering.hpp"
#include "arv/ui/core/ui.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace arv::web {

struct WebBlockRenderAssets {
  client::ClientBlockRenderAssets loaded{};
  client::ParticleAssets particle_assets{};
  std::vector<std::filesystem::path> pack_cache_paths{};
  std::string registry_snapshot_json{};
  bool ready{};
};

struct WebEntityRenderAssets {
  client::ClientEntityRenderAssets loaded{};
  client::ItemAnimationLibrary item_animation_library{};
  asset::TextureAtlas item_atlas{};
  ui::UiImageData crosshair_image{};
  std::vector<std::filesystem::path> pack_cache_paths{};
  std::string registry_snapshot_json{};
  client::PlayerSkinReceiptMap player_skin_handles{};
  bool ready{};
};

} // namespace arv::web
