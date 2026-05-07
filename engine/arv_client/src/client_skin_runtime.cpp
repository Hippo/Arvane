#include "client_skin_runtime.hpp"

#include "arv/asset/resource.hpp"

#include "client_protocol_send.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>
#include <utility>

namespace arv::client::detail {
namespace {

[[nodiscard]] std::optional<std::vector<u8>> read_binary_file(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  return std::vector<u8>{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
}

[[nodiscard]] bool decoded_png_matches(
    std::span<const u8> bytes,
    u16 width,
    u16 height,
    std::string_view asset_suffix) {
  auto image = asset::decode_png_image_bytes(
      bytes,
      {"arvane:texture/skin/" + std::string{asset_suffix}});
  return image &&
         image.value().width == width &&
         image.value().height == height &&
         image.value().pixels.size() ==
             static_cast<usize>(width) * static_cast<usize>(height) * 4U;
}

[[nodiscard]] bool cached_skin_is_valid(
    const LocalPlayerIdentity& identity,
    const protocol::SkinDescriptor& skin) {
  if (identity.arvane_home.empty() || skin.skin_hash.empty() ||
      !protocol::is_valid_skin_hash(skin.skin_hash)) {
    return false;
  }
  std::optional<std::vector<u8>> bytes =
      read_binary_file(cached_skin_path(identity.arvane_home, skin.skin_hash));
  if (!bytes.has_value()) {
    return false;
  }
  return protocol::skin_hash_for_bytes(*bytes) == skin.skin_hash &&
         decoded_png_matches(*bytes, skin.width, skin.height, skin.skin_hash);
}

[[nodiscard]] bool write_cached_skin(
    const LocalPlayerIdentity& identity,
    std::string_view skin_hash,
    std::span<const u8> bytes) {
  if (identity.arvane_home.empty()) {
    return false;
  }
  const std::filesystem::path path = cached_skin_path(identity.arvane_home, skin_hash);
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return false;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return false;
  }
  output.write(
      reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(output);
}

} // namespace

u32 ClientSkinRuntime::next_request_id() noexcept {
  return next_request_id_++;
}

void ClientSkinRuntime::handle_skin_rules(
    const LocalPlayerIdentity& identity,
    ClientProtocolSender& sender,
    protocol::ClientboundSkinRules rules) {
  rules_ = std::move(rules);
  has_rules_ = true;
  pending_upload_.reset();
  if (!rules_.enabled || !rules_.allow_uploads || identity.arvane_home.empty()) {
    return;
  }

  const LocalProfileSkin* skin =
      best_local_skin_for_max_resolution(identity, rules_.max_res);
  if (skin == nullptr || skin->skin_hash.empty() ||
      skin->skin_hash == rules_.selected_skin_hash) {
    return;
  }
  std::optional<std::vector<u8>> bytes =
      read_binary_file(local_skin_path(identity.arvane_home, *skin));
  if (!bytes.has_value() || bytes->empty()) {
    return;
  }
  if (protocol::skin_hash_for_bytes(*bytes) != skin->skin_hash) {
    return;
  }
  if (!decoded_png_matches(*bytes, skin->width, skin->height, skin->skin_hash)) {
    return;
  }

  protocol::ServerboundSkinUploadBegin begin;
  begin.format = skin->format;
  begin.client_skin_hash_hint = skin->skin_hash;
  begin.width = skin->width;
  begin.height = skin->height;
  begin.total_upload_bytes = static_cast<u32>(bytes->size());
  if (sender.send_skin_upload_begin(begin)) {
    pending_upload_ = PendingUpload{std::move(begin), std::move(*bytes)};
  }
}

void ClientSkinRuntime::maybe_request_profile_skin(
    const LocalPlayerIdentity& identity,
    ClientProtocolSender& sender,
    const protocol::SkinDescriptor& skin) {
  if (skin.skin_hash.empty() || !protocol::is_valid_skin_hash(skin.skin_hash)) {
    return;
  }
  if (cached_skin_is_valid(identity, skin) ||
      requested_hashes_.find(skin.skin_hash) != requested_hashes_.end()) {
    return;
  }
  const u32 request_id = next_request_id();
  if (sender.send_skin_request({request_id, skin.skin_hash})) {
    requested_hash_by_id_[request_id] = skin.skin_hash;
    requested_hashes_.insert(skin.skin_hash);
  }
}

void ClientSkinRuntime::handle_profile_info(
    const LocalPlayerIdentity& identity,
    ClientProtocolSender& sender,
    protocol::ClientboundPlayerProfileInfo info) {
  protocol::PlayerProfileInfo stored;
  stored.player_id = info.player_id;
  stored.profile_id = std::move(info.profile_id);
  stored.display_name = std::move(info.display_name);
  stored.skin = std::move(info.skin);
  maybe_request_profile_skin(identity, sender, stored.skin);
  profiles_by_player_[stored.player_id] = std::move(stored);
}

void ClientSkinRuntime::handle_profile_info_batch(
    const LocalPlayerIdentity& identity,
    ClientProtocolSender& sender,
    protocol::ClientboundPlayerProfileInfoBatch batch) {
  for (protocol::PlayerProfileInfo& profile : batch.profiles) {
    maybe_request_profile_skin(identity, sender, profile.skin);
    profiles_by_player_[profile.player_id] = std::move(profile);
  }
}

void ClientSkinRuntime::handle_skin_data(
    const LocalPlayerIdentity& identity,
    protocol::ClientboundSkinData data) {
  const auto request = requested_hash_by_id_.find(data.request_id);
  if (request == requested_hash_by_id_.end() || request->second != data.skin_hash) {
    return;
  }
  if (protocol::skin_hash_for_bytes(data.compressed_image_bytes) == data.skin_hash &&
      decoded_png_matches(
          data.compressed_image_bytes,
          data.width,
          data.height,
          data.skin_hash)) {
    (void)write_cached_skin(identity, data.skin_hash, data.compressed_image_bytes);
  }
  requested_hashes_.erase(request->second);
  requested_hash_by_id_.erase(request);
}

void ClientSkinRuntime::handle_skin_data_begin_chunked(
    protocol::ClientboundSkinDataBeginChunked begin) {
  const auto request = requested_hash_by_id_.find(begin.request_id);
  if (request == requested_hash_by_id_.end() || request->second != begin.skin_hash ||
      begin.total_bytes > protocol::MaxSkinImagePayloadBytes) {
    return;
  }
  PendingDownload download;
  download.skin_hash = std::move(begin.skin_hash);
  download.format = begin.format;
  download.width = begin.width;
  download.height = begin.height;
  download.total_bytes = begin.total_bytes;
  download.bytes.reserve(begin.total_bytes);
  downloads_[begin.request_id] = std::move(download);
}

void ClientSkinRuntime::handle_skin_data_chunk(protocol::ClientboundSkinDataChunk chunk) {
  auto download = downloads_.find(chunk.request_id);
  if (download == downloads_.end()) {
    return;
  }
  if (chunk.chunk.empty() ||
      download->second.bytes.size() + chunk.chunk.size() > download->second.total_bytes) {
    downloads_.erase(download);
    return;
  }
  download->second.bytes.insert(
      download->second.bytes.end(),
      chunk.chunk.begin(),
      chunk.chunk.end());
}

void ClientSkinRuntime::handle_skin_data_end_chunked(
    const LocalPlayerIdentity& identity,
    protocol::ClientboundSkinDataEndChunked end) {
  auto download = downloads_.find(end.request_id);
  auto request = requested_hash_by_id_.find(end.request_id);
  if (download == downloads_.end() || request == requested_hash_by_id_.end()) {
    return;
  }
  if (download->second.bytes.size() == download->second.total_bytes &&
      protocol::skin_hash_for_bytes(download->second.bytes) == download->second.skin_hash &&
      decoded_png_matches(
          download->second.bytes,
          download->second.width,
          download->second.height,
          download->second.skin_hash)) {
    (void)write_cached_skin(identity, download->second.skin_hash, download->second.bytes);
  }
  requested_hashes_.erase(request->second);
  requested_hash_by_id_.erase(request);
  downloads_.erase(download);
}

void ClientSkinRuntime::handle_skin_request_deny(protocol::ClientboundSkinRequestDeny deny) {
  const auto request = requested_hash_by_id_.find(deny.request_id);
  if (request != requested_hash_by_id_.end()) {
    requested_hashes_.erase(request->second);
    requested_hash_by_id_.erase(request);
  }
  downloads_.erase(deny.request_id);
}

void ClientSkinRuntime::handle_skin_upload_accept(
    ClientProtocolSender& sender,
    protocol::ClientboundSkinUploadAccept accept) {
  if (!pending_upload_.has_value() || accept.max_chunk_bytes == 0U) {
    return;
  }
  if (accept.upload_mode == protocol::SkinUploadMode::Full) {
    (void)sender.send_skin_upload_data({accept.upload_id, pending_upload_->bytes});
    return;
  }

  usize offset = 0;
  while (offset < pending_upload_->bytes.size()) {
    const usize chunk_size = std::min<usize>(
        accept.max_chunk_bytes,
        pending_upload_->bytes.size() - offset);
    protocol::ServerboundSkinUploadChunk chunk;
    chunk.upload_id = accept.upload_id;
    chunk.image_chunk_bytes.insert(
        chunk.image_chunk_bytes.end(),
        pending_upload_->bytes.begin() + static_cast<std::ptrdiff_t>(offset),
        pending_upload_->bytes.begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
    if (!sender.send_skin_upload_chunk(chunk)) {
      return;
    }
    offset += chunk_size;
  }
  (void)sender.send_skin_upload_end({accept.upload_id});
}

void ClientSkinRuntime::handle_skin_upload_deny(protocol::ClientboundSkinUploadDeny) {
  pending_upload_.reset();
}

void ClientSkinRuntime::handle_skin_upload_complete(protocol::ClientboundSkinUploadComplete complete) {
  rules_.selected_skin_hash = complete.skin.skin_hash;
  pending_upload_.reset();
}

void ClientSkinRuntime::handle_skin_upload_failed(protocol::ClientboundSkinUploadFailed) {
  pending_upload_.reset();
}

const protocol::PlayerProfileInfo* ClientSkinRuntime::profile_for_player(
    PlayerID player_id) const noexcept {
  const auto found = profiles_by_player_.find(player_id);
  return found == profiles_by_player_.end() ? nullptr : &found->second;
}

std::vector<protocol::PlayerProfileInfo> ClientSkinRuntime::profiles() const {
  std::vector<protocol::PlayerProfileInfo> out;
  out.reserve(profiles_by_player_.size());
  for (const auto& [player_id, profile] : profiles_by_player_) {
    (void)player_id;
    out.push_back(profile);
  }
  return out;
}

} // namespace arv::client::detail
