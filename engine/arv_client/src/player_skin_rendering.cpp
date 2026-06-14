#include "arv/client/player_skin_rendering.hpp"

#include "arv/io/files.hpp"

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
