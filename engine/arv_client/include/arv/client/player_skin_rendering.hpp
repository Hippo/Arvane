#pragma once

#include "arv/asset/resource.hpp"
#include "arv/base/ui/screens/screen.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/render/render_types.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::render {
struct PlayerSkinTextureSnapshot;
}

namespace arv::client {

using PlayerSkinReceiptMap = HashMap<std::string, render::TextureRegionHandle>;

inline constexpr std::string_view DefaultMenuPlayerSkinKey =
    "arvane:default_skin/default";
inline constexpr std::string_view DefaultMenuPlayerSlimSkinKey =
    "arvane:default_skin/default_slim";
inline constexpr std::string_view DefaultMenuPlayerSkinTextureAsset =
    "arvane:texture/default_skin/default";
inline constexpr std::string_view DefaultMenuPlayerSlimSkinTextureAsset =
    "arvane:texture/default_skin/default_slim";

struct PlayerSkinUploadSummary {
  usize attempted{};
  usize uploaded{};
};

struct PlayerSkinReceiptRefreshCallbacks {
  FunctionRef<bool(std::string_view, render::TextureRegionHandle)> is_resident{};
  FunctionRef<std::optional<render::TextureRegionHandle>(
      std::string_view,
      const asset::Image&)> upload{};
};

struct PlayerSkinReceiptRefreshSummary {
  usize pruned{};
  PlayerSkinUploadSummary upload{};
};

struct PlayerSkinReceiptLookupCallbacks {
  FunctionRef<bool(std::string_view, render::TextureRegionHandle)> ready{};
  FunctionRef<void(render::TextureRegionHandle)> mark_used{};
};

struct MenuPlayerSkinTextureViews {
  HashMap<std::string, ui::PlayerSkinTextureView> textures{};
  ui::PlayerSkinTextureView default_texture{};
};

struct MenuPlayerSkinTextureViewCallbacks {
  FunctionRef<std::optional<ui::PlayerSkinTextureView>(
      std::string_view,
      render::TextureRegionHandle)> texture_view{};
  FunctionRef<void(render::TextureRegionHandle)> mark_used{};
};

[[nodiscard]] std::string_view menu_player_skin_default_key(bool slim_model) noexcept;

[[nodiscard]] std::filesystem::path player_skin_render_path(
    const LocalPlayerIdentity& identity,
    std::string_view skin_hash);

[[nodiscard]] std::optional<asset::Image> load_valid_player_skin_render_image(
    const LocalPlayerIdentity& identity,
    const protocol::SkinDescriptor& skin);

[[nodiscard]] HashMap<std::string, asset::Image> collect_missing_player_skin_render_images(
    const LocalPlayerIdentity& identity,
    std::span<const protocol::PlayerProfileInfo> profiles,
    const PlayerSkinReceiptMap& resident_skins);

[[nodiscard]] HashMap<std::string, asset::Image> collect_missing_menu_player_skin_render_images(
    const std::filesystem::path& arvane_home,
    const LocalProfileStore& profiles,
    std::span<const LocalProfileSkin> pending_create_skins,
    const HashMap<std::string, std::vector<LocalProfileSkin>>& pending_edit_skins,
    const PlayerSkinReceiptMap& resident_skins,
    FunctionRef<std::optional<asset::Image>(std::string_view)> load_default_texture);

[[nodiscard]] PlayerSkinUploadSummary upload_player_skin_render_images_to_receipts(
    PlayerSkinReceiptMap& resident_skins,
    const HashMap<std::string, asset::Image>& images,
    FunctionRef<std::optional<render::TextureRegionHandle>(
        std::string_view,
        const asset::Image&)> upload);

[[nodiscard]] PlayerSkinReceiptRefreshSummary refresh_player_skin_render_receipts(
    PlayerSkinReceiptMap& resident_skins,
    const LocalPlayerIdentity& identity,
    std::span<const protocol::PlayerProfileInfo> profiles,
    PlayerSkinReceiptRefreshCallbacks callbacks);

[[nodiscard]] usize prune_player_skin_receipts(
    PlayerSkinReceiptMap& resident_skins,
    FunctionRef<bool(
        std::string_view,
        render::TextureRegionHandle)> is_resident);

[[nodiscard]] std::optional<render::TextureRegionHandle> ready_player_skin_receipt(
    const PlayerSkinReceiptMap& resident_skins,
    std::string_view skin_hash,
    PlayerSkinReceiptLookupCallbacks callbacks);

[[nodiscard]] ui::PlayerSkinTextureView player_skin_texture_view_from_snapshot(
    const render::PlayerSkinTextureSnapshot& snapshot) noexcept;

[[nodiscard]] MenuPlayerSkinTextureViews menu_player_skin_texture_views_from_receipts(
    const PlayerSkinReceiptMap& resident_skins,
    MenuPlayerSkinTextureViewCallbacks callbacks);

[[nodiscard]] std::optional<render::TextureRegionHandle> menu_player_skin_preview_region(
    const PlayerSkinReceiptMap& resident_skins,
    std::string_view requested_skin_hash,
    bool slim_model);

[[nodiscard]] std::optional<asset::Image> load_valid_local_profile_skin_render_image(
    const std::filesystem::path& arvane_home,
    const LocalProfileSkin& skin);

} // namespace arv::client
