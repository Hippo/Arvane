#include "arv/server/skin_runtime.hpp"

#include "arv/asset/resource.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>
#include <system_error>
#include <vector>

namespace arv::server {

std::filesystem::path server_skin_storage_path(
    const std::filesystem::path& server_save_root,
    std::string_view skin_hash) {
  return server_save_root / "player_assets" / "skins" / (std::string{skin_hash} + ".png");
}

const io::SkinResolutionConfig* find_skin_resolution_rule(
    const io::SkinConfig& config,
    u16 size) noexcept {
  for (const io::SkinResolutionConfig& resolution : config.resolutions) {
    if (resolution.size == size) {
      return &resolution;
    }
  }
  return nullptr;
}

u16 max_allowed_skin_resolution(
    const io::SkinConfig& config,
    const player::ResolvedPermissions& permissions) noexcept {
  u16 max_resolution = 0;
  if (!config.enabled) {
    return max_resolution;
  }
  for (const io::SkinResolutionConfig& resolution : config.resolutions) {
    if (!resolution.enabled) {
      continue;
    }
    if (!resolution.permission.empty() && !permissions.has(resolution.permission)) {
      continue;
    }
    max_resolution = std::max(max_resolution, resolution.size);
  }
  return max_resolution;
}

protocol::SkinUploadMode choose_skin_upload_mode(
    const io::SkinConfig& config,
    u16 width,
    u16 height,
    u32 total_upload_bytes) noexcept {
  if (width >= 512U || height >= 512U ||
      total_upload_bytes >= config.uploads.chunk_threshold_bytes) {
    return protocol::SkinUploadMode::Chunked;
  }
  return protocol::SkinUploadMode::Full;
}

Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason> validate_skin_upload_begin(
    const io::SkinConfig& config,
    const player::ResolvedPermissions& permissions,
    bool authenticated,
    bool has_active_upload,
    bool rate_limited,
    bool active_upload_limit_reached,
    const protocol::ServerboundSkinUploadBegin& begin) noexcept {
  if (!authenticated) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::NotAuthenticated);
  }
  if (!config.enabled) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::SkinsDisabled);
  }
  if (!config.allow_uploads) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::UploadsDisabled);
  }
  if (rate_limited) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::RateLimited);
  }
  if (has_active_upload) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::UploadAlreadyPending);
  }
  if (active_upload_limit_reached) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::RateLimited);
  }
  if (begin.format != protocol::SkinImageFormat::Png) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::InvalidFormat);
  }
  if (begin.width == 0U || begin.height == 0U || begin.width != begin.height) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::InvalidResolution);
  }
  const io::SkinResolutionConfig* resolution =
      find_skin_resolution_rule(config, begin.width);
  if (resolution == nullptr) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::InvalidResolution);
  }
  if (!resolution->enabled) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::ResolutionNotAllowed);
  }
  if (!resolution->permission.empty() && !permissions.has(resolution->permission)) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::ResolutionNotAllowed);
  }
  if (begin.total_upload_bytes == 0U ||
      begin.total_upload_bytes > resolution->max_upload_bytes) {
    return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::UploadTooLarge);
  }

  return Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>::success(
      {resolution,
       choose_skin_upload_mode(config, begin.width, begin.height, begin.total_upload_bytes)});
}

u64 current_skin_storage_bytes(const std::filesystem::path& server_save_root) {
  const std::filesystem::path root = server_save_root / "player_assets" / "skins";
  std::error_code error;
  if (!std::filesystem::exists(root, error)) {
    return 0U;
  }

  u64 total = 0U;
  for (std::filesystem::recursive_directory_iterator iter{
           root,
           std::filesystem::directory_options::skip_permission_denied,
           error};
       !error && iter != std::filesystem::recursive_directory_iterator{};
       iter.increment(error)) {
    if (!iter->is_regular_file(error)) {
      continue;
    }
    const std::filesystem::path path = iter->path();
    if (path.extension() != ".png") {
      continue;
    }
    error.clear();
    const u64 size = static_cast<u64>(iter->file_size(error));
    if (!error) {
      total += size;
    }
  }
  return total;
}

Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>
validate_and_store_uploaded_skin(
    const std::filesystem::path& server_save_root,
    const io::SkinResolutionConfig& resolution,
    const protocol::ServerboundSkinUploadBegin& begin,
    std::span<const u8> compressed_image_bytes,
    u64 current_storage_bytes,
    u64 max_total_storage_bytes,
    u64* added_storage_bytes) {
  if (added_storage_bytes != nullptr) {
    *added_storage_bytes = 0U;
  }
  if (compressed_image_bytes.size() != begin.total_upload_bytes ||
      compressed_image_bytes.empty() ||
      compressed_image_bytes.size() > resolution.max_upload_bytes) {
    return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::UploadTooLarge);
  }

  auto image = asset::decode_png_image_bytes(
      compressed_image_bytes,
      {"arvane:texture/skin/upload"});
  if (!image) {
    return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::DecodeFailed);
  }
  if (image.value().width != begin.width || image.value().height != begin.height) {
    return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::InvalidResolution);
  }
  const usize decoded_bytes =
      static_cast<usize>(image.value().width) * static_cast<usize>(image.value().height) * 4U;
  if (image.value().pixels.size() != decoded_bytes ||
      decoded_bytes > resolution.max_decoded_bytes) {
    return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::DecodeFailed);
  }

  const std::string authoritative_hash =
      protocol::skin_hash_for_bytes(compressed_image_bytes);
  const std::filesystem::path output_path =
      server_skin_storage_path(server_save_root, authoritative_hash);
  std::error_code error;
  std::filesystem::create_directories(output_path.parent_path(), error);
  if (error) {
    return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::ServerStorageFull);
  }
  const bool output_exists = std::filesystem::exists(output_path, error);
  if (error) {
    return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
        protocol::SkinUploadDenyReason::ServerStorageFull);
  }
  if (!output_exists) {
    if (current_storage_bytes > max_total_storage_bytes ||
        static_cast<u64>(compressed_image_bytes.size()) >
            max_total_storage_bytes - current_storage_bytes) {
      return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
          protocol::SkinUploadDenyReason::ServerStorageFull);
    }

    const std::filesystem::path temp_path = output_path.string() + ".tmp";
    std::filesystem::remove(temp_path, error);
    error.clear();
    std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
    if (!output) {
      return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
          protocol::SkinUploadDenyReason::ServerStorageFull);
    }
    output.write(
        reinterpret_cast<const char*>(compressed_image_bytes.data()),
        static_cast<std::streamsize>(compressed_image_bytes.size()));
    if (!output) {
      return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
          protocol::SkinUploadDenyReason::ServerStorageFull);
    }
    output.close();
    if (!output) {
      return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
          protocol::SkinUploadDenyReason::ServerStorageFull);
    }
    std::filesystem::rename(temp_path, output_path, error);
    if (error) {
      std::filesystem::remove(temp_path, error);
      return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::failure(
          protocol::SkinUploadDenyReason::ServerStorageFull);
    }
    if (added_storage_bytes != nullptr) {
      *added_storage_bytes = static_cast<u64>(compressed_image_bytes.size());
    }
  }

  protocol::SkinDescriptor descriptor;
  descriptor.skin_hash = authoritative_hash;
  descriptor.format = protocol::SkinImageFormat::Png;
  descriptor.width = begin.width;
  descriptor.height = begin.height;
  descriptor.slim_model = begin.slim_model;
  return Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>::success(
      std::move(descriptor));
}

Expected<protocol::SkinDescriptor, protocol::SkinRequestDenyReason>
load_stored_skin_descriptor(
    const std::filesystem::path& server_save_root,
    std::string_view skin_hash) {
  if (!protocol::is_valid_skin_hash(skin_hash)) {
    return Expected<protocol::SkinDescriptor, protocol::SkinRequestDenyReason>::failure(
        protocol::SkinRequestDenyReason::InvalidHash);
  }
  const std::filesystem::path path = server_skin_storage_path(server_save_root, skin_hash);
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Expected<protocol::SkinDescriptor, protocol::SkinRequestDenyReason>::failure(
        protocol::SkinRequestDenyReason::NotFound);
  }
  std::error_code error;
  const u64 file_size = std::filesystem::file_size(path, error);
  if (error || file_size == 0U || file_size > protocol::MaxSkinImagePayloadBytes) {
    return Expected<protocol::SkinDescriptor, protocol::SkinRequestDenyReason>::failure(
        protocol::SkinRequestDenyReason::TooLarge);
  }
  std::vector<u8> bytes{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
  if (bytes.empty() || protocol::skin_hash_for_bytes(bytes) != skin_hash) {
    return Expected<protocol::SkinDescriptor, protocol::SkinRequestDenyReason>::failure(
        protocol::SkinRequestDenyReason::InternalError);
  }
  auto image = asset::decode_png_image_bytes(
      bytes,
      {"arvane:texture/skin/" + std::string{skin_hash}});
  if (!image) {
    return Expected<protocol::SkinDescriptor, protocol::SkinRequestDenyReason>::failure(
        protocol::SkinRequestDenyReason::InternalError);
  }
  if (image.value().width <= 0 || image.value().height <= 0 ||
      image.value().width > std::numeric_limits<u16>::max() ||
      image.value().height > std::numeric_limits<u16>::max()) {
    return Expected<protocol::SkinDescriptor, protocol::SkinRequestDenyReason>::failure(
        protocol::SkinRequestDenyReason::InternalError);
  }
  protocol::SkinDescriptor descriptor;
  descriptor.skin_hash = std::string{skin_hash};
  descriptor.format = protocol::SkinImageFormat::Png;
  descriptor.width = static_cast<u16>(image.value().width);
  descriptor.height = static_cast<u16>(image.value().height);
  return Expected<protocol::SkinDescriptor, protocol::SkinRequestDenyReason>::success(
      std::move(descriptor));
}

} // namespace arv::server
