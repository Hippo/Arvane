#pragma once

#include "asset_helpers.hpp"

#include "arv/asset/font.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/client/item_animation.hpp"
#include "arv/entity/components.hpp"
#include "arv/io/config.hpp"
#include "arv/lang/language.hpp"
#include "arv/player/item.hpp"
#include "arv/render/render_commands.hpp"
#include "arv/ui/core/ui.hpp"
#include "arv/voxel/block.hpp"
#include "arv/world/world_generation.hpp"

#include <string>

namespace arv::desktop {

struct DesktopLoadedAssets {
  voxel::BlockRegistry block_registry{};
  u64 block_registry_hash{};
  player::ItemRegistry item_registry{};
  u64 item_registry_hash{};
  client::ItemAnimationLibrary item_animation_library{};
  entity::EntityRegistry entity_registry{};
  lang::LanguageCatalog language{};
  asset::BlockTextureAtlas block_atlas{};
  asset::TextureAtlas item_atlas{};
  EntityRenderAssets entity_render_assets{};
  asset::FontSystem font_system{};
  ui::UiFontStacks ui_font_stacks{};
  world::WorldGenerationConfig default_generation_config{};
};

struct DesktopAssetLoadError {
  std::string category{};
  std::string message{};
};

using DesktopLoadedAssetsResult = Expected<DesktopLoadedAssets, DesktopAssetLoadError>;

[[nodiscard]] DesktopLoadedAssetsResult load_desktop_assets(
    asset::ResourceManager& resources,
    render::RenderCommandQueue& renderer,
    const io::EngineConfig& config,
    const ui::UiTheme& theme);

} // namespace arv::desktop
