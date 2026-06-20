#include "desktop_frame_loop.hpp"

#include "arv/client/player_skin_rendering.hpp"
#include "arv/client/render_asset_upload.hpp"
#include "arv/io/files.hpp"
#include "arv/protocol/skins.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::desktop {

void DesktopFrameLoop::clear_player_skin_render_cache() {
  player_skin_regions_.clear();
  menu_state_.set_player_skin_texture_views({}, {});
  invalidate_ui_tree_cache();
}

void DesktopFrameLoop::refresh_player_skin_render_assets(DesktopGameSession& active_session) {
  if (active_session.client == nullptr) {
    return;
  }

  const client::LocalPlayerIdentity& identity =
      active_session.client->local_identity();
  const std::vector<protocol::PlayerProfileInfo> profiles =
      active_session.client->player_profile_infos();
  const auto upload_skin =
      [&](std::string_view skin_hash,
          const asset::Image& image) -> std::optional<render::TextureRegionHandle> {
    return renderer_.upload_player_skin_texture(
        std::string{skin_hash},
        client::texture_upload_from_image(image));
  };
  (void)client::refresh_player_skin_render_receipts(
      player_skin_regions_,
      identity,
      profiles,
      {.upload =
           FunctionRef<std::optional<render::TextureRegionHandle>(
               std::string_view,
               const asset::Image&)>{upload_skin}});
}

void DesktopFrameLoop::refresh_menu_profile_render_assets() {
  const auto load_default_skin =
      [&](std::string_view texture_asset) -> std::optional<asset::Image> {
    auto texture = resources_.load_texture({std::string{texture_asset}});
    if (!texture) {
      return std::nullopt;
    }
    const asset::TextureResource* resource = resources_.texture(texture.value());
    if (resource == nullptr || resource->image.empty()) {
      return std::nullopt;
    }
    return resource->image;
  };
  HashMap<std::string, asset::Image> new_images =
      client::collect_missing_menu_player_skin_render_images(
          config_.paths.arvane_home_path(),
          menu_state_.local_profile_store(),
          menu_state_.pending_create_skins(),
          menu_state_.pending_edit_skins(),
          player_skin_regions_,
          FunctionRef<std::optional<asset::Image>(std::string_view)>{
              load_default_skin});

  const auto upload_skin =
      [&](std::string_view skin_hash,
          const asset::Image& image) -> std::optional<render::TextureRegionHandle> {
    return renderer_.upload_player_skin_texture(
        std::string{skin_hash},
        client::texture_upload_from_image(image));
  };
  (void)client::upload_player_skin_render_images_to_receipts(
      player_skin_regions_,
      new_images,
      FunctionRef<std::optional<render::TextureRegionHandle>(
          std::string_view,
          const asset::Image&)>{upload_skin});
  if (!new_images.empty()) {
    invalidate_ui_tree_cache();
  }

  const auto skin_resident =
      [&](std::string_view skin_hash, render::TextureRegionHandle handle) {
    return renderer_.player_skin_texture_snapshot(skin_hash, handle).has_value();
  };
  (void)client::prune_player_skin_receipts(
      player_skin_regions_,
      FunctionRef<bool(
          std::string_view,
          render::TextureRegionHandle)>{skin_resident});

  const auto texture_view =
      [&](std::string_view skin_hash,
          render::TextureRegionHandle handle)
      -> std::optional<ui::PlayerSkinTextureView> {
    std::optional<render::PlayerSkinTextureSnapshot> snapshot =
        renderer_.player_skin_texture_snapshot(skin_hash, handle);
    if (!snapshot.has_value()) {
      return std::nullopt;
    }
    return client::player_skin_texture_view_from_snapshot(snapshot.value());
  };
  const auto mark_used = [&](render::TextureRegionHandle handle) {
    (void)renderer_.mark_player_skin_texture_used(handle, true);
  };
  client::MenuPlayerSkinTextureViews texture_views =
      client::menu_player_skin_texture_views_from_receipts(
          player_skin_regions_,
          {.texture_view =
               FunctionRef<std::optional<ui::PlayerSkinTextureView>(
                   std::string_view,
                   render::TextureRegionHandle)>{texture_view},
           .mark_used =
               FunctionRef<void(render::TextureRegionHandle)>{mark_used}});
  menu_state_.set_player_skin_texture_views(
      std::move(texture_views.textures),
      texture_views.default_texture);
}

} // namespace arv::desktop
