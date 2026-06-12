#include "desktop_frame_loop.hpp"

#include "asset_helpers.hpp"
#include "desktop_skin_textures.hpp"

#include "arv/io/files.hpp"
#include "arv/protocol/skins.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::desktop {
namespace {

[[nodiscard]] std::filesystem::path player_skin_render_path(
    const client::LocalPlayerIdentity& identity,
    std::string_view skin_hash) {
  for (const client::LocalProfileSkin& local_skin : identity.skins) {
    if (local_skin.skin_hash == skin_hash) {
      return client::local_skin_path(identity.arvane_home, local_skin);
    }
  }
  return client::cached_skin_path(identity.arvane_home, skin_hash);
}

[[nodiscard]] std::optional<asset::Image> load_valid_player_skin_render_image(
    const client::LocalPlayerIdentity& identity,
    const protocol::SkinDescriptor& skin) {
  if (identity.arvane_home.empty() || skin.skin_hash.empty() ||
      !protocol::is_valid_skin_hash(skin.skin_hash)) {
    return std::nullopt;
  }
  std::optional<std::vector<u8>> bytes =
      io::try_read_binary_file(player_skin_render_path(identity, skin.skin_hash));
  if (!bytes.has_value() ||
      protocol::skin_hash_for_bytes(*bytes) != skin.skin_hash) {
    return std::nullopt;
  }

  auto image = asset::decode_png_image_bytes(
      *bytes,
      {"arvane:texture/player_skin/" + skin.skin_hash});
  if (!image || image.value().width != skin.width ||
      image.value().height != skin.height ||
      image.value().pixels.size() !=
          static_cast<usize>(skin.width) * static_cast<usize>(skin.height) * 4U) {
    return std::nullopt;
  }
  return std::move(image.value());
}

[[nodiscard]] std::optional<asset::Image> load_valid_local_profile_skin_render_image(
    const std::filesystem::path& arvane_home,
    const client::LocalProfileSkin& skin) {
  if (skin.skin_hash.empty() || !protocol::is_valid_skin_hash(skin.skin_hash)) {
    return std::nullopt;
  }
  std::optional<std::vector<u8>> bytes =
      io::try_read_binary_file(client::local_skin_path(arvane_home, skin));
  if (!bytes.has_value() ||
      protocol::skin_hash_for_bytes(*bytes) != skin.skin_hash) {
    return std::nullopt;
  }

  auto image = asset::decode_png_image_bytes(
      *bytes,
      {"arvane:texture/player_skin/" + skin.skin_hash});
  if (!image || image.value().width != skin.width ||
      image.value().height != skin.height ||
      image.value().pixels.size() !=
          static_cast<usize>(skin.width) * static_cast<usize>(skin.height) * 4U) {
    return std::nullopt;
  }
  return std::move(image.value());
}

[[nodiscard]] ui::PlayerSkinTextureView player_skin_texture_view(
    const render::PlayerSkinTextureSnapshot& snapshot) noexcept {
  ui::PlayerSkinTextureView view;
  view.valid = true;
  view.page = snapshot.region.page_index;
  view.atlas_width = snapshot.atlas_width;
  view.atlas_height = snapshot.atlas_height;
  view.x = snapshot.region.x;
  view.y = snapshot.region.y;
  view.width = snapshot.region.width;
  view.height = snapshot.region.height;
  view.image_width = snapshot.image_width;
  view.image_height = snapshot.image_height;
  return view;
}

} // namespace

void DesktopFrameLoop::clear_player_skin_render_cache() {
  player_skin_regions_.clear();
  menu_state_.set_player_skin_texture_views({}, {});
  invalidate_ui_tree_cache();
}

void DesktopFrameLoop::refresh_player_skin_render_assets(DesktopGameSession& active_session) {
  if (active_session.client == nullptr) {
    return;
  }

  HashMap<std::string, asset::Image> new_images;
  const client::LocalPlayerIdentity& identity =
      active_session.client->local_identity();
  for (const protocol::PlayerProfileInfo& profile :
       active_session.client->player_profile_infos()) {
    const std::string& skin_hash = profile.skin.skin_hash;
    if (skin_hash.empty() ||
        player_skin_regions_.find(skin_hash) != player_skin_regions_.end() ||
        new_images.find(skin_hash) != new_images.end()) {
      continue;
    }
    std::optional<asset::Image> image =
        load_valid_player_skin_render_image(identity, profile.skin);
    if (image.has_value()) {
      new_images[skin_hash] = std::move(image.value());
    }
  }
  if (new_images.empty()) {
    return;
  }

  for (const auto& [skin_hash, image] : new_images) {
    std::optional<render::TextureRegionHandle> handle =
        renderer_.upload_player_skin_texture(skin_hash, texture_upload_from_image(image));
    if (handle.has_value()) {
      player_skin_regions_[skin_hash] = handle.value();
    }
  }
}

void DesktopFrameLoop::refresh_menu_profile_render_assets() {
  HashMap<std::string, asset::Image> new_images;

  const auto queue_default_skin =
      [&](std::string_view key, std::string_view texture_asset) {
        if (player_skin_regions_.find(std::string{key}) != player_skin_regions_.end() ||
            new_images.find(std::string{key}) != new_images.end()) {
          return;
        }
        auto texture = resources_.load_texture({std::string{texture_asset}});
        if (texture) {
          const asset::TextureResource* resource = resources_.texture(texture.value());
          if (resource != nullptr && !resource->image.empty()) {
            new_images[std::string{key}] = resource->image;
          }
        }
      };
  queue_default_skin(DefaultMenuPlayerSkinKey, "arvane:texture/default_skin/default");
  queue_default_skin(
      DefaultMenuPlayerSlimSkinKey,
      "arvane:texture/default_skin/default_slim");

  const client::LocalProfileStore& store = menu_state_.local_profile_store();
  const auto queue_skin = [&](const client::LocalProfileSkin& skin) {
    if (skin.skin_hash.empty() ||
        player_skin_regions_.find(skin.skin_hash) != player_skin_regions_.end() ||
        new_images.find(skin.skin_hash) != new_images.end()) {
      return;
    }
    std::optional<asset::Image> image =
        load_valid_local_profile_skin_render_image(
            config_.paths.arvane_home_path(),
            skin);
    if (image.has_value()) {
      new_images[skin.skin_hash] = std::move(image.value());
    }
  };
  for (const client::LocalPlayerIdentity& profile : store.profiles) {
    for (const client::LocalProfileSkin& skin : profile.skins) {
      queue_skin(skin);
    }
  }
  for (const client::LocalProfileSkin& skin : menu_state_.pending_create_skins()) {
    queue_skin(skin);
  }
  for (const auto& [profile_id, skins] : menu_state_.pending_edit_skins()) {
    (void)profile_id;
    for (const client::LocalProfileSkin& skin : skins) {
      queue_skin(skin);
    }
  }

  for (const auto& [skin_hash, image] : new_images) {
    std::optional<render::TextureRegionHandle> handle =
        renderer_.upload_player_skin_texture(skin_hash, texture_upload_from_image(image));
    if (handle.has_value()) {
      player_skin_regions_[skin_hash] = handle.value();
    }
  }
  if (!new_images.empty()) {
    invalidate_ui_tree_cache();
  }

  HashMap<std::string, ui::PlayerSkinTextureView> texture_views;
  ui::PlayerSkinTextureView default_texture;
  for (auto skin = player_skin_regions_.begin(); skin != player_skin_regions_.end();) {
    const render::TextureRegionHandle handle = skin->second;
    std::optional<render::PlayerSkinTextureSnapshot> snapshot =
        renderer_.player_skin_texture_snapshot(skin->first, handle);
    if (!snapshot.has_value()) {
      skin = player_skin_regions_.erase(skin);
      continue;
    }
    (void)renderer_.mark_player_skin_texture_used(handle, true);
    ui::PlayerSkinTextureView view = player_skin_texture_view(snapshot.value());
    if (skin->first == DefaultMenuPlayerSkinKey) {
      default_texture = view;
    } else if (skin->first != DefaultMenuPlayerSlimSkinKey) {
      texture_views[skin->first] = view;
    }
    ++skin;
  }
  menu_state_.set_player_skin_texture_views(std::move(texture_views), default_texture);
}

} // namespace arv::desktop
