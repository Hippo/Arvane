#include "web_player_skins.hpp"

#include "arv/client/player_skin_rendering.hpp"
#include "arv/client/render_asset_upload.hpp"
#include "arv/core/types.hpp"
#include "web_application.hpp"
#include "web_platform.hpp"
#include "web_render_host.hpp"

#include <optional>
#include <utility>
#include <vector>

namespace arv::web {

void reset_web_player_skin_render_assets() {
  g_app.entity_assets.player_skin_handles.clear();
  configure_web_player_skin_atlas({});
}

bool upload_web_player_skin_texture(
    std::string skin_hash,
    const asset::Image& image) {
  const std::string requested_skin_hash = std::move(skin_hash);
  if (requested_skin_hash.empty() ||
      g_app.entity_assets.player_skin_handles.find(requested_skin_hash) !=
          g_app.entity_assets.player_skin_handles.end()) {
    return true;
  }

  HashMap<std::string, asset::Image> images;
  images[requested_skin_hash] = image;
  const auto upload_skin =
      [](std::string_view hash,
         const asset::Image& upload_image) -> std::optional<render::TextureRegionHandle> {
    return upload_web_player_skin_texture_to_render_host(
        std::string{hash},
        client::texture_upload_from_image(upload_image));
  };
  const client::PlayerSkinUploadSummary summary =
      client::upload_player_skin_render_images_to_receipts(
          g_app.entity_assets.player_skin_handles,
          images,
          FunctionRef<std::optional<render::TextureRegionHandle>(
              std::string_view,
              const asset::Image&)>{upload_skin});
  return summary.uploaded > 0U;
}

void refresh_web_player_skin_render_assets() {
  if (g_app.session == nullptr || !g_app.entity_assets.ready) {
    return;
  }

  const client::LocalPlayerIdentity& identity = g_app.session->local_identity();
  const std::vector<protocol::PlayerProfileInfo> profiles =
      g_app.session->player_profile_infos();
  const auto skin_resident =
      [](std::string_view skin_hash, render::TextureRegionHandle handle) {
    return web_player_skin_texture_ready(skin_hash, handle);
  };
  const auto upload_skin =
      [](std::string_view skin_hash,
         const asset::Image& image) -> std::optional<render::TextureRegionHandle> {
    return upload_web_player_skin_texture_to_render_host(
        std::string{skin_hash},
        client::texture_upload_from_image(image));
  };
  (void)client::refresh_player_skin_render_receipts(
      g_app.entity_assets.player_skin_handles,
      identity,
      profiles,
      {.is_resident =
           FunctionRef<bool(
               std::string_view,
               render::TextureRegionHandle)>{skin_resident},
       .upload =
           FunctionRef<std::optional<render::TextureRegionHandle>(
               std::string_view,
               const asset::Image&)>{upload_skin}});
}

} // namespace arv::web
