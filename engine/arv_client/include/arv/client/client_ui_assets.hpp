#pragma once

#include "arv/asset/font.hpp"
#include "arv/asset/pack.hpp"
#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/entity/components.hpp"
#include "arv/player/item.hpp"
#include "arv/render/render_types.hpp"
#include "arv/text/language.hpp"
#include "arv/ui/core/ui.hpp"
#include "arv/voxel/block.hpp"

#include <string>

namespace arv::client {

struct UiFontAssetLoadError {
  std::string category{"assets"};
  std::string message{};
};

struct UiFontFallbackWarning {
  asset::AssetID asset{};
  std::string message{};
};

struct ClientDisplayNameApplyError {
  std::string category{"lang"};
  std::string message{};
};

struct UiFontStackLoadOptions {
  bool include_fallbacks{};
  FunctionRef<void(const UiFontFallbackWarning&)> fallback_warning{};
};

using UiFontStackLoadResult = Expected<void, UiFontAssetLoadError>;
using UiFontStacksLoadResult = Expected<ui::UiFontStacks, UiFontAssetLoadError>;
using ClientDisplayNameApplyResult =
    Expected<void, ClientDisplayNameApplyError>;

struct ClientUiTextureAssets {
  asset::TextureAtlas item_atlas{};
  ui::UiImageData crosshair_image{};
};

using ClientUiTextureAssetsResult =
    Expected<ClientUiTextureAssets, asset::AssetError>;

struct UiDrawCommandAppendResult {
  usize ui_rects_appended{};
  usize ui_textured_quads_appended{};
  usize ui_glyphs_appended{};
  bool font_atlas_synced{};
  bool rolled_back{};
};

[[nodiscard]] text::LanguageResult load_pack_language(
    const asset::PackManager& packs,
    std::string locale = std::string{text::DefaultLocale},
    std::string fallback_locale = std::string{text::DefaultLocale});

[[nodiscard]] ClientDisplayNameApplyResult apply_client_block_display_names(
    const text::LanguageCatalog& language,
    voxel::BlockRegistry& registry);

[[nodiscard]] ClientDisplayNameApplyResult apply_client_item_display_names(
    const text::LanguageCatalog& language,
    const voxel::BlockRegistry& blocks,
    player::ItemRegistry& registry);

[[nodiscard]] ClientDisplayNameApplyResult apply_client_entity_display_names(
    const text::LanguageCatalog& language,
    entity::EntityRegistry& registry);

[[nodiscard]] UiFontStackLoadResult load_ui_font_stack(
    asset::ResourceManager& resources,
    asset::FontSystem& font_system,
    std::string asset_name,
    u32 pixel_size,
    asset::FontStack& stack,
    UiFontStackLoadOptions options = {});

[[nodiscard]] UiFontStacksLoadResult load_ui_font_stacks(
    asset::ResourceManager& resources,
    asset::FontSystem& font_system,
    const ui::UiTheme& theme,
    const ui::UiDisplayMetrics& metrics,
    FunctionRef<void(const UiFontFallbackWarning&)> fallback_warning = {});

[[nodiscard]] ClientUiTextureAssetsResult load_client_ui_texture_assets(
    asset::ResourceManager& resources,
    const player::ItemRegistry& item_registry,
    asset::TextureAtlasBuildOptions item_atlas_options =
        {.max_page_size = 1024, .max_pages = 1, .padding = 2});

[[nodiscard]] bool sync_ui_font_atlas(
    asset::FontSystem& font_system,
    FunctionRef<bool(const render::TextureArrayUploadData&)> upload);

[[nodiscard]] UiDrawCommandAppendResult append_ui_draw_commands_with_font_sync(
    const ui::UiTree& tree,
    asset::FontSystem& font_system,
    const ui::UiFontStacks& font_stacks,
    render::FramePacket& frame,
    const ui::UiDisplayMetrics& metrics,
    f32 time_seconds,
    FunctionRef<bool()> sync_font_atlas);

} // namespace arv::client
