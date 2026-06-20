#include "arv/client/player_skin_rendering.hpp"

#include "arv/io/files.hpp"
#include "arv/render/render_commands.hpp"

#include <utility>

namespace arv::client {
namespace {

[[nodiscard]] std::optional<asset::Image> decode_valid_skin_image(
    std::span<const u8> bytes,
    std::string_view skin_hash,
    u16 width,
    u16 height) {
  auto image = asset::decode_png_image_bytes(
      bytes,
      {"arvane:texture/player_skin/" + std::string{skin_hash}});
  if (!image ||
      image.value().width != width ||
      image.value().height != height ||
      image.value().pixels.size() !=
          static_cast<usize>(width) * static_cast<usize>(height) * 4U) {
    return std::nullopt;
  }
  return std::move(image.value());
}

} // namespace

std::string_view menu_player_skin_default_key(bool slim_model) noexcept {
  return slim_model ? DefaultMenuPlayerSlimSkinKey : DefaultMenuPlayerSkinKey;
}

std::filesystem::path player_skin_render_path(
    const LocalPlayerIdentity& identity,
    std::string_view skin_hash) {
  for (const LocalProfileSkin& local_skin : identity.skins) {
    if (local_skin.skin_hash == skin_hash) {
      return local_skin_path(identity.arvane_home, local_skin);
    }
  }
  return cached_skin_path(identity.arvane_home, skin_hash);
}

std::optional<asset::Image> load_valid_player_skin_render_image(
    const LocalPlayerIdentity& identity,
    const protocol::SkinDescriptor& skin) {
  if (identity.arvane_home.empty() ||
      skin.skin_hash.empty() ||
      !protocol::is_valid_skin_hash(skin.skin_hash)) {
    return std::nullopt;
  }

  std::optional<std::vector<u8>> bytes =
      io::try_read_binary_file(player_skin_render_path(identity, skin.skin_hash));
  if (!bytes.has_value() ||
      protocol::skin_hash_for_bytes(*bytes) != skin.skin_hash) {
    return std::nullopt;
  }
  return decode_valid_skin_image(*bytes, skin.skin_hash, skin.width, skin.height);
}

HashMap<std::string, asset::Image> collect_missing_player_skin_render_images(
    const LocalPlayerIdentity& identity,
    std::span<const protocol::PlayerProfileInfo> profiles,
    const PlayerSkinReceiptMap& resident_skins) {
  HashMap<std::string, asset::Image> images;
  for (const protocol::PlayerProfileInfo& profile : profiles) {
    const std::string& skin_hash = profile.skin.skin_hash;
    if (skin_hash.empty() ||
        resident_skins.find(skin_hash) != resident_skins.end() ||
        images.find(skin_hash) != images.end()) {
      continue;
    }
    std::optional<asset::Image> image =
        load_valid_player_skin_render_image(identity, profile.skin);
    if (image.has_value()) {
      images[skin_hash] = std::move(image.value());
    }
  }
  return images;
}

HashMap<std::string, asset::Image> collect_missing_menu_player_skin_render_images(
    const std::filesystem::path& arvane_home,
    const LocalProfileStore& profiles,
    std::span<const LocalProfileSkin> pending_create_skins,
    const HashMap<std::string, std::vector<LocalProfileSkin>>& pending_edit_skins,
    const PlayerSkinReceiptMap& resident_skins,
    FunctionRef<std::optional<asset::Image>(std::string_view)> load_default_texture) {
  HashMap<std::string, asset::Image> images;

  const auto has_skin = [&](std::string_view skin_hash) {
    return skin_hash.empty() ||
           resident_skins.find(std::string{skin_hash}) != resident_skins.end() ||
           images.find(std::string{skin_hash}) != images.end();
  };

  const auto queue_default_skin =
      [&](std::string_view key, std::string_view texture_asset) {
    if (!load_default_texture || has_skin(key)) {
      return;
    }
    std::optional<asset::Image> image = load_default_texture(texture_asset);
    if (image.has_value() && !image->empty()) {
      images[std::string{key}] = std::move(image.value());
    }
  };
  queue_default_skin(DefaultMenuPlayerSkinKey, DefaultMenuPlayerSkinTextureAsset);
  queue_default_skin(
      DefaultMenuPlayerSlimSkinKey,
      DefaultMenuPlayerSlimSkinTextureAsset);

  const auto queue_profile_skin = [&](const LocalProfileSkin& skin) {
    if (has_skin(skin.skin_hash)) {
      return;
    }
    std::optional<asset::Image> image =
        load_valid_local_profile_skin_render_image(arvane_home, skin);
    if (image.has_value()) {
      images[skin.skin_hash] = std::move(image.value());
    }
  };

  for (const LocalPlayerIdentity& profile : profiles.profiles) {
    for (const LocalProfileSkin& skin : profile.skins) {
      queue_profile_skin(skin);
    }
  }
  for (const LocalProfileSkin& skin : pending_create_skins) {
    queue_profile_skin(skin);
  }
  for (const auto& [profile_id, skins] : pending_edit_skins) {
    (void)profile_id;
    for (const LocalProfileSkin& skin : skins) {
      queue_profile_skin(skin);
    }
  }

  return images;
}

PlayerSkinUploadSummary upload_player_skin_render_images_to_receipts(
    PlayerSkinReceiptMap& resident_skins,
    const HashMap<std::string, asset::Image>& images,
    FunctionRef<std::optional<render::TextureRegionHandle>(
        std::string_view,
        const asset::Image&)> upload) {
  PlayerSkinUploadSummary summary;
  if (!upload) {
    return summary;
  }
  for (const auto& [skin_hash, image] : images) {
    if (skin_hash.empty() ||
        resident_skins.find(skin_hash) != resident_skins.end()) {
      continue;
    }
    ++summary.attempted;
    if (image.empty() ||
        image.pixels.size() !=
            static_cast<usize>(image.width) * static_cast<usize>(image.height) * 4U) {
      continue;
    }
    std::optional<render::TextureRegionHandle> handle = upload(skin_hash, image);
    if (handle.has_value() && handle->valid()) {
      resident_skins[skin_hash] = handle.value();
      ++summary.uploaded;
    }
  }
  return summary;
}

PlayerSkinReceiptRefreshSummary refresh_player_skin_render_receipts(
    PlayerSkinReceiptMap& resident_skins,
    const LocalPlayerIdentity& identity,
    std::span<const protocol::PlayerProfileInfo> profiles,
    PlayerSkinReceiptRefreshCallbacks callbacks) {
  PlayerSkinReceiptRefreshSummary summary;
  if (callbacks.is_resident) {
    summary.pruned = prune_player_skin_receipts(
        resident_skins,
        FunctionRef<bool(
            std::string_view,
            render::TextureRegionHandle)>{callbacks.is_resident});
  }
  const HashMap<std::string, asset::Image> images =
      collect_missing_player_skin_render_images(
          identity,
          profiles,
          resident_skins);
  summary.upload = upload_player_skin_render_images_to_receipts(
      resident_skins,
      images,
      FunctionRef<std::optional<render::TextureRegionHandle>(
          std::string_view,
          const asset::Image&)>{callbacks.upload});
  return summary;
}

usize prune_player_skin_receipts(
    PlayerSkinReceiptMap& resident_skins,
    FunctionRef<bool(
        std::string_view,
        render::TextureRegionHandle)> is_resident) {
  if (!is_resident) {
    return 0U;
  }
  usize removed{};
  for (auto skin = resident_skins.begin(); skin != resident_skins.end();) {
    const render::TextureRegionHandle handle = skin->second;
    if (!handle.valid() || !is_resident(skin->first, handle)) {
      skin = resident_skins.erase(skin);
      ++removed;
      continue;
    }
    ++skin;
  }
  return removed;
}

std::optional<render::TextureRegionHandle> ready_player_skin_receipt(
    const PlayerSkinReceiptMap& resident_skins,
    std::string_view skin_hash,
    PlayerSkinReceiptLookupCallbacks callbacks) {
  if (skin_hash.empty() || !callbacks.ready) {
    return std::nullopt;
  }
  const auto found = resident_skins.find(std::string{skin_hash});
  if (found == resident_skins.end() || !found->second.valid()) {
    return std::nullopt;
  }
  if (!callbacks.ready(skin_hash, found->second)) {
    return std::nullopt;
  }
  if (callbacks.mark_used) {
    callbacks.mark_used(found->second);
  }
  return found->second;
}

ui::PlayerSkinTextureView player_skin_texture_view_from_snapshot(
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

MenuPlayerSkinTextureViews menu_player_skin_texture_views_from_receipts(
    const PlayerSkinReceiptMap& resident_skins,
    MenuPlayerSkinTextureViewCallbacks callbacks) {
  MenuPlayerSkinTextureViews views;
  if (!callbacks.texture_view) {
    return views;
  }

  for (const auto& [skin_hash, handle] : resident_skins) {
    if (!handle.valid()) {
      continue;
    }
    std::optional<ui::PlayerSkinTextureView> view =
        callbacks.texture_view(skin_hash, handle);
    if (!view.has_value() || !view->valid) {
      continue;
    }
    if (callbacks.mark_used) {
      callbacks.mark_used(handle);
    }
    if (skin_hash == DefaultMenuPlayerSkinKey) {
      views.default_texture = view.value();
    } else if (skin_hash != DefaultMenuPlayerSlimSkinKey) {
      views.textures[skin_hash] = view.value();
    }
  }
  return views;
}

std::optional<render::TextureRegionHandle> menu_player_skin_preview_region(
    const PlayerSkinReceiptMap& resident_skins,
    std::string_view requested_skin_hash,
    bool slim_model) {
  const auto find_valid =
      [&](std::string_view skin_hash) -> std::optional<render::TextureRegionHandle> {
    if (skin_hash.empty()) {
      return std::nullopt;
    }
    const auto found = resident_skins.find(std::string{skin_hash});
    if (found == resident_skins.end() || !found->second.valid()) {
      return std::nullopt;
    }
    return found->second;
  };

  if (std::optional<render::TextureRegionHandle> handle =
          find_valid(requested_skin_hash);
      handle.has_value()) {
    return handle;
  }
  if (std::optional<render::TextureRegionHandle> handle =
          find_valid(menu_player_skin_default_key(slim_model));
      handle.has_value()) {
    return handle;
  }
  return find_valid(DefaultMenuPlayerSkinKey);
}

std::optional<asset::Image> load_valid_local_profile_skin_render_image(
    const std::filesystem::path& arvane_home,
    const LocalProfileSkin& skin) {
  if (skin.skin_hash.empty() || !protocol::is_valid_skin_hash(skin.skin_hash)) {
    return std::nullopt;
  }

  std::optional<std::vector<u8>> bytes =
      io::try_read_binary_file(local_skin_path(arvane_home, skin));
  if (!bytes.has_value() ||
      protocol::skin_hash_for_bytes(*bytes) != skin.skin_hash) {
    return std::nullopt;
  }
  return decode_valid_skin_image(*bytes, skin.skin_hash, skin.width, skin.height);
}

} // namespace arv::client
