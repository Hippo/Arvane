#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::protocol {

inline constexpr usize SkinHashTextLength = 66U;
inline constexpr u32 MaxSkinImagePayloadBytes = 16U * 1024U * 1024U;
inline constexpr u32 MaxSkinRequestBatchEntries = 256U;
inline constexpr u32 MaxPlayerProfileInfoBatchEntries = 4096U;

enum class SkinImageFormat : u8 {
  Png = 0,
};

struct SkinDescriptor {
  std::string skin_hash{}; // Empty means no custom skin.
  SkinImageFormat format{SkinImageFormat::Png};
  u16 width{};
  u16 height{};
  bool slim_model{};
};

enum class SkinUploadMode : u8 {
  Full = 0,
  Chunked = 1,
};

struct SkinRequestEntry {
  u32 request_id{};
  std::string skin_hash{};
};

struct PlayerProfileInfo {
  PlayerID player_id{};
  std::string profile_id{};
  std::string display_name{};
  SkinDescriptor skin{};
};

enum class SkinRequestDenyReason : u8 {
  SkinsDisabled,
  InvalidHash,
  NotFound,
  NotAdvertisedToClient,
  RateLimited,
  TooLarge,
  InternalError,
};

enum class SkinUploadDenyReason : u8 {
  SkinsDisabled,
  UploadsDisabled,
  NotAuthenticated,
  InvalidFormat,
  InvalidResolution,
  ResolutionNotAllowed,
  UploadTooLarge,
  RateLimited,
  UploadAlreadyPending,
  ServerStorageFull,
  DecodeFailed,
  HashMismatch,
  InternalError,
};

struct ClientboundSkinRules {
  bool enabled{};
  bool allow_uploads{};
  u16 max_res{};
  SkinDescriptor selected_skin{};
};

struct ClientboundPlayerProfileInfo {
  PlayerID player_id{};
  std::string profile_id{};
  std::string display_name{};
  SkinDescriptor skin{};
};

struct ClientboundPlayerProfileInfoBatch {
  std::vector<PlayerProfileInfo> profiles{};
};

struct ServerboundSkinRequest {
  u32 request_id{};
  std::string skin_hash{};
};

struct ServerboundSkinRequestBatch {
  std::vector<SkinRequestEntry> requests{};
};

struct ClientboundSkinData {
  u32 request_id{};
  std::string skin_hash{};
  SkinImageFormat format{SkinImageFormat::Png};
  u16 width{};
  u16 height{};
  std::vector<u8> compressed_image_bytes{};
};

struct ClientboundSkinDataBeginChunked {
  u32 request_id{};
  std::string skin_hash{};
  SkinImageFormat format{SkinImageFormat::Png};
  u16 width{};
  u16 height{};
  u32 total_bytes{};
};

struct ClientboundSkinDataChunk {
  u32 request_id{};
  std::vector<u8> chunk{};
};

struct ClientboundSkinDataEndChunked {
  u32 request_id{};
};

struct ClientboundSkinRequestDeny {
  u32 request_id{};
  std::string skin_hash{};
  SkinRequestDenyReason reason{SkinRequestDenyReason::InternalError};
};

struct ServerboundSkinUploadBegin {
  SkinImageFormat format{SkinImageFormat::Png};
  std::string client_skin_hash_hint{};
  u16 width{};
  u16 height{};
  bool slim_model{};
  u32 total_upload_bytes{};
};

struct ClientboundSkinUploadAccept {
  u32 upload_id{};
  SkinUploadMode upload_mode{SkinUploadMode::Full};
  u32 max_chunk_bytes{};
};

struct ClientboundSkinUploadDeny {
  SkinUploadDenyReason reason{SkinUploadDenyReason::InternalError};
};

struct ServerboundSkinUploadData {
  u32 upload_id{};
  std::vector<u8> compressed_image_bytes{};
};

struct ServerboundSkinUploadChunk {
  u32 upload_id{};
  std::vector<u8> image_chunk_bytes{};
};

struct ServerboundSkinUploadEnd {
  u32 upload_id{};
};

struct ClientboundSkinUploadComplete {
  u32 upload_id{};
  SkinDescriptor skin{};
};

struct ClientboundSkinUploadFailed {
  u32 upload_id{};
  SkinUploadDenyReason reason{SkinUploadDenyReason::InternalError};
};

[[nodiscard]] bool is_valid_skin_hash(std::string_view text) noexcept;
[[nodiscard]] std::string skin_hash_for_bytes(std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_rules(const ClientboundSkinRules& rules);
[[nodiscard]] Expected<ClientboundSkinRules, ProtocolError> deserialize_skin_rules(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_player_profile_info(
    const ClientboundPlayerProfileInfo& info);
[[nodiscard]] Expected<ClientboundPlayerProfileInfo, ProtocolError>
deserialize_player_profile_info(std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_player_profile_info_batch(
    const ClientboundPlayerProfileInfoBatch& batch);
[[nodiscard]] Expected<ClientboundPlayerProfileInfoBatch, ProtocolError>
deserialize_player_profile_info_batch(std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_request(const ServerboundSkinRequest& request);
[[nodiscard]] Expected<ServerboundSkinRequest, ProtocolError> deserialize_skin_request(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_request_batch(
    const ServerboundSkinRequestBatch& batch);
[[nodiscard]] Expected<ServerboundSkinRequestBatch, ProtocolError>
deserialize_skin_request_batch(std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_data(const ClientboundSkinData& data);
[[nodiscard]] Expected<ClientboundSkinData, ProtocolError> deserialize_skin_data(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_data_begin_chunked(
    const ClientboundSkinDataBeginChunked& begin);
[[nodiscard]] Expected<ClientboundSkinDataBeginChunked, ProtocolError>
deserialize_skin_data_begin_chunked(std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_data_chunk(const ClientboundSkinDataChunk& chunk);
[[nodiscard]] Expected<ClientboundSkinDataChunk, ProtocolError> deserialize_skin_data_chunk(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_data_end_chunked(
    const ClientboundSkinDataEndChunked& end);
[[nodiscard]] Expected<ClientboundSkinDataEndChunked, ProtocolError>
deserialize_skin_data_end_chunked(std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_request_deny(
    const ClientboundSkinRequestDeny& deny);
[[nodiscard]] Expected<ClientboundSkinRequestDeny, ProtocolError> deserialize_skin_request_deny(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_upload_begin(
    const ServerboundSkinUploadBegin& begin);
[[nodiscard]] Expected<ServerboundSkinUploadBegin, ProtocolError> deserialize_skin_upload_begin(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_upload_accept(
    const ClientboundSkinUploadAccept& accept);
[[nodiscard]] Expected<ClientboundSkinUploadAccept, ProtocolError> deserialize_skin_upload_accept(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_upload_deny(
    const ClientboundSkinUploadDeny& deny);
[[nodiscard]] Expected<ClientboundSkinUploadDeny, ProtocolError> deserialize_skin_upload_deny(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_upload_data(
    const ServerboundSkinUploadData& data);
[[nodiscard]] Expected<ServerboundSkinUploadData, ProtocolError> deserialize_skin_upload_data(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_upload_chunk(
    const ServerboundSkinUploadChunk& chunk);
[[nodiscard]] Expected<ServerboundSkinUploadChunk, ProtocolError> deserialize_skin_upload_chunk(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_upload_end(const ServerboundSkinUploadEnd& end);
[[nodiscard]] Expected<ServerboundSkinUploadEnd, ProtocolError> deserialize_skin_upload_end(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_upload_complete(
    const ClientboundSkinUploadComplete& complete);
[[nodiscard]] Expected<ClientboundSkinUploadComplete, ProtocolError>
deserialize_skin_upload_complete(std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_skin_upload_failed(
    const ClientboundSkinUploadFailed& failed);
[[nodiscard]] Expected<ClientboundSkinUploadFailed, ProtocolError>
deserialize_skin_upload_failed(std::span<const u8> bytes);

} // namespace arv::protocol
