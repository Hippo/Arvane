#pragma once

#include "arv/core/expected.hpp"
#include "arv/io/config.hpp"
#include "arv/player/permissions.hpp"
#include "arv/protocol/skins.hpp"

#include <filesystem>
#include <span>

namespace arv::server {

struct SkinUploadBeginAccepted {
  const io::SkinResolutionConfig* resolution{};
  protocol::SkinUploadMode upload_mode{protocol::SkinUploadMode::Full};
};

[[nodiscard]] std::filesystem::path server_skin_storage_path(
    const std::filesystem::path& server_save_root,
    std::string_view skin_hash);

[[nodiscard]] const io::SkinResolutionConfig* find_skin_resolution_rule(
    const io::SkinConfig& config,
    u16 size) noexcept;

[[nodiscard]] u16 max_allowed_skin_resolution(
    const io::SkinConfig& config,
    const player::ResolvedPermissions& permissions) noexcept;

[[nodiscard]] protocol::SkinUploadMode choose_skin_upload_mode(
    const io::SkinConfig& config,
    u16 width,
    u16 height,
    u32 total_upload_bytes) noexcept;

[[nodiscard]] Expected<SkinUploadBeginAccepted, protocol::SkinUploadDenyReason>
validate_skin_upload_begin(
    const io::SkinConfig& config,
    const player::ResolvedPermissions& permissions,
    bool authenticated,
    bool has_active_upload,
    bool rate_limited,
    const protocol::ServerboundSkinUploadBegin& begin) noexcept;

[[nodiscard]] Expected<protocol::SkinDescriptor, protocol::SkinUploadDenyReason>
validate_and_store_uploaded_skin(
    const std::filesystem::path& server_save_root,
    const io::SkinResolutionConfig& resolution,
    const protocol::ServerboundSkinUploadBegin& begin,
    std::span<const u8> compressed_image_bytes);

[[nodiscard]] Expected<protocol::SkinDescriptor, protocol::SkinRequestDenyReason>
load_stored_skin_descriptor(
    const std::filesystem::path& server_save_root,
    std::string_view skin_hash);

} // namespace arv::server
