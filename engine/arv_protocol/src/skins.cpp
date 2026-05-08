#include "arv/protocol/skins.hpp"

#include "arv/crypto/hash.hpp"

#include <cstddef>
#include <utility>

namespace arv::protocol {
namespace {

[[nodiscard]] std::span<const std::byte> as_bytes(std::span<const u8> bytes) noexcept {
  return {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()};
}

[[nodiscard]] bool valid_hash_or_empty(std::string_view text) noexcept {
  return text.empty() || is_valid_skin_hash(text);
}

[[nodiscard]] Expected<void, ProtocolError> require_finished(
    const MessageReader& reader,
    std::string_view packet_name) {
  if (reader.remaining() != 0U) {
    return Expected<void, ProtocolError>::failure(
        ProtocolError{"trailing bytes in " + std::string{packet_name}});
  }
  return Expected<void, ProtocolError>::success();
}

[[nodiscard]] Expected<SkinImageFormat, ProtocolError> read_skin_format(MessageReader& reader) {
  auto raw = reader.read<u8>();
  if (!raw) {
    return Expected<SkinImageFormat, ProtocolError>::failure(raw.error());
  }
  if (raw.value() != static_cast<u8>(SkinImageFormat::Png)) {
    return Expected<SkinImageFormat, ProtocolError>::failure(
        ProtocolError{"unsupported skin image format"});
  }
  return Expected<SkinImageFormat, ProtocolError>::success(SkinImageFormat::Png);
}

[[nodiscard]] Expected<SkinUploadMode, ProtocolError> read_upload_mode(MessageReader& reader) {
  auto raw = reader.read<u8>();
  if (!raw) {
    return Expected<SkinUploadMode, ProtocolError>::failure(raw.error());
  }
  switch (static_cast<SkinUploadMode>(raw.value())) {
    case SkinUploadMode::Full:
    case SkinUploadMode::Chunked:
      return Expected<SkinUploadMode, ProtocolError>::success(
          static_cast<SkinUploadMode>(raw.value()));
  }
  return Expected<SkinUploadMode, ProtocolError>::failure(
      ProtocolError{"unsupported skin upload mode"});
}

[[nodiscard]] Expected<SkinRequestDenyReason, ProtocolError> read_request_deny_reason(
    MessageReader& reader) {
  auto raw = reader.read<u8>();
  if (!raw) {
    return Expected<SkinRequestDenyReason, ProtocolError>::failure(raw.error());
  }
  switch (static_cast<SkinRequestDenyReason>(raw.value())) {
    case SkinRequestDenyReason::SkinsDisabled:
    case SkinRequestDenyReason::InvalidHash:
    case SkinRequestDenyReason::NotFound:
    case SkinRequestDenyReason::NotAdvertisedToClient:
    case SkinRequestDenyReason::RateLimited:
    case SkinRequestDenyReason::TooLarge:
    case SkinRequestDenyReason::InternalError:
      return Expected<SkinRequestDenyReason, ProtocolError>::success(
          static_cast<SkinRequestDenyReason>(raw.value()));
  }
  return Expected<SkinRequestDenyReason, ProtocolError>::failure(
      ProtocolError{"unsupported skin request deny reason"});
}

[[nodiscard]] Expected<SkinUploadDenyReason, ProtocolError> read_upload_deny_reason(
    MessageReader& reader) {
  auto raw = reader.read<u8>();
  if (!raw) {
    return Expected<SkinUploadDenyReason, ProtocolError>::failure(raw.error());
  }
  switch (static_cast<SkinUploadDenyReason>(raw.value())) {
    case SkinUploadDenyReason::SkinsDisabled:
    case SkinUploadDenyReason::UploadsDisabled:
    case SkinUploadDenyReason::NotAuthenticated:
    case SkinUploadDenyReason::InvalidFormat:
    case SkinUploadDenyReason::InvalidResolution:
    case SkinUploadDenyReason::ResolutionNotAllowed:
    case SkinUploadDenyReason::UploadTooLarge:
    case SkinUploadDenyReason::RateLimited:
    case SkinUploadDenyReason::UploadAlreadyPending:
    case SkinUploadDenyReason::ServerStorageFull:
    case SkinUploadDenyReason::DecodeFailed:
    case SkinUploadDenyReason::HashMismatch:
    case SkinUploadDenyReason::InternalError:
      return Expected<SkinUploadDenyReason, ProtocolError>::success(
          static_cast<SkinUploadDenyReason>(raw.value()));
  }
  return Expected<SkinUploadDenyReason, ProtocolError>::failure(
      ProtocolError{"unsupported skin upload deny reason"});
}

void write_skin_descriptor(MessageWriter& writer, const SkinDescriptor& descriptor) {
  writer.write_string(descriptor.skin_hash);
  writer.write<SkinImageFormat>(descriptor.format);
  writer.write<u16>(descriptor.width);
  writer.write<u16>(descriptor.height);
  writer.write_bool(descriptor.slim_model);
}

[[nodiscard]] Expected<SkinDescriptor, ProtocolError> read_skin_descriptor(
    MessageReader& reader) {
  auto hash = reader.read_string();
  if (!hash) {
    return Expected<SkinDescriptor, ProtocolError>::failure(hash.error());
  }
  if (!valid_hash_or_empty(hash.value())) {
    return Expected<SkinDescriptor, ProtocolError>::failure(
        ProtocolError{"invalid skin descriptor hash"});
  }
  auto format = read_skin_format(reader);
  auto width = reader.read<u16>();
  auto height = reader.read<u16>();
  auto slim_model = reader.read_bool();
  if (!format || !width || !height || !slim_model) {
    return Expected<SkinDescriptor, ProtocolError>::failure(
        ProtocolError{"failed to read skin descriptor"});
  }
  SkinDescriptor descriptor;
  descriptor.skin_hash = std::move(hash.value());
  descriptor.format = format.value();
  descriptor.width = width.value();
  descriptor.height = height.value();
  descriptor.slim_model = slim_model.value();
  return Expected<SkinDescriptor, ProtocolError>::success(std::move(descriptor));
}

void write_profile_info(MessageWriter& writer, const PlayerProfileInfo& info) {
  writer.write<u32>(info.player_id.value);
  writer.write_string(info.profile_id);
  writer.write_string(info.display_name);
  write_skin_descriptor(writer, info.skin);
}

[[nodiscard]] Expected<PlayerProfileInfo, ProtocolError> read_profile_info(
    MessageReader& reader) {
  auto player_id = reader.read<u32>();
  auto profile_id = reader.read_string();
  auto display_name = reader.read_string();
  if (!player_id || !profile_id || !display_name) {
    return Expected<PlayerProfileInfo, ProtocolError>::failure(
        ProtocolError{"failed to read player profile info"});
  }
  auto skin = read_skin_descriptor(reader);
  if (!skin) {
    return Expected<PlayerProfileInfo, ProtocolError>::failure(skin.error());
  }
  PlayerProfileInfo info;
  info.player_id = PlayerID{player_id.value()};
  info.profile_id = std::move(profile_id.value());
  info.display_name = std::move(display_name.value());
  info.skin = std::move(skin.value());
  return Expected<PlayerProfileInfo, ProtocolError>::success(std::move(info));
}

void write_blob(MessageWriter& writer, std::span<const u8> bytes) {
  writer.write<u32>(static_cast<u32>(bytes.size()));
  writer.write_bytes(bytes);
}

[[nodiscard]] Expected<std::vector<u8>, ProtocolError> read_blob(
    MessageReader& reader,
    u32 max_bytes,
    std::string_view field_name) {
  auto length = reader.read<u32>();
  if (!length) {
    return Expected<std::vector<u8>, ProtocolError>::failure(length.error());
  }
  if (length.value() > max_bytes) {
    return Expected<std::vector<u8>, ProtocolError>::failure(
        ProtocolError{std::string{field_name} + " exceeds protocol limit"});
  }
  std::vector<u8> bytes(length.value());
  if (auto result = reader.read_bytes(std::span<u8>{bytes.data(), bytes.size()}); !result) {
    return Expected<std::vector<u8>, ProtocolError>::failure(result.error());
  }
  return Expected<std::vector<u8>, ProtocolError>::success(std::move(bytes));
}

[[nodiscard]] Expected<void, ProtocolError> validate_required_hash(
    std::string_view hash,
    std::string_view field_name) {
  if (!is_valid_skin_hash(hash)) {
    return Expected<void, ProtocolError>::failure(
        ProtocolError{std::string{field_name} + " must be a lowercase 0x-prefixed skin hash"});
  }
  return Expected<void, ProtocolError>::success();
}

} // namespace

bool is_valid_skin_hash(std::string_view text) noexcept {
  return text.size() == SkinHashTextLength && crypto::is_prefixed_lower_hex(text);
}

std::string skin_hash_for_bytes(std::span<const u8> bytes) {
  const crypto::Hash256 hash = crypto::hash_256(as_bytes(bytes));
  return crypto::to_prefixed_lower_hex(hash.bytes);
}

std::vector<u8> serialize_skin_rules(const ClientboundSkinRules& rules) {
  MessageWriter writer;
  writer.write_bool(rules.enabled);
  writer.write_bool(rules.allow_uploads);
  writer.write<u16>(rules.max_res);
  write_skin_descriptor(writer, rules.selected_skin);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSkinRules, ProtocolError> deserialize_skin_rules(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ClientboundSkinRules rules;
  auto enabled = reader.read_bool();
  auto allow_uploads = reader.read_bool();
  auto max_res = reader.read<u16>();
  if (!enabled || !allow_uploads || !max_res) {
    return Expected<ClientboundSkinRules, ProtocolError>::failure(
        ProtocolError{"failed to read skin rules"});
  }
  auto selected_skin = read_skin_descriptor(reader);
  if (!selected_skin) {
    return Expected<ClientboundSkinRules, ProtocolError>::failure(selected_skin.error());
  }
  if (auto result = require_finished(reader, "skin rules"); !result) {
    return Expected<ClientboundSkinRules, ProtocolError>::failure(result.error());
  }
  rules.enabled = enabled.value();
  rules.allow_uploads = allow_uploads.value();
  rules.max_res = max_res.value();
  rules.selected_skin = std::move(selected_skin.value());
  return Expected<ClientboundSkinRules, ProtocolError>::success(std::move(rules));
}

std::vector<u8> serialize_player_profile_info(const ClientboundPlayerProfileInfo& info) {
  MessageWriter writer;
  write_profile_info(
      writer,
      {info.player_id, info.profile_id, info.display_name, info.skin});
  return std::move(writer).take_bytes();
}

Expected<ClientboundPlayerProfileInfo, ProtocolError> deserialize_player_profile_info(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto info = read_profile_info(reader);
  if (!info) {
    return Expected<ClientboundPlayerProfileInfo, ProtocolError>::failure(info.error());
  }
  if (auto result = require_finished(reader, "player profile info"); !result) {
    return Expected<ClientboundPlayerProfileInfo, ProtocolError>::failure(result.error());
  }
  return Expected<ClientboundPlayerProfileInfo, ProtocolError>::success(
      {info.value().player_id,
       std::move(info.value().profile_id),
       std::move(info.value().display_name),
       std::move(info.value().skin)});
}

std::vector<u8> serialize_player_profile_info_batch(
    const ClientboundPlayerProfileInfoBatch& batch) {
  MessageWriter writer;
  writer.write<u32>(static_cast<u32>(batch.profiles.size()));
  for (const PlayerProfileInfo& profile : batch.profiles) {
    write_profile_info(writer, profile);
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundPlayerProfileInfoBatch, ProtocolError> deserialize_player_profile_info_batch(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto count = reader.read<u32>();
  if (!count) {
    return Expected<ClientboundPlayerProfileInfoBatch, ProtocolError>::failure(count.error());
  }
  if (count.value() > MaxPlayerProfileInfoBatchEntries) {
    return Expected<ClientboundPlayerProfileInfoBatch, ProtocolError>::failure(
        ProtocolError{"too many player profile info entries"});
  }
  ClientboundPlayerProfileInfoBatch batch;
  batch.profiles.reserve(count.value());
  for (u32 index = 0; index < count.value(); ++index) {
    auto profile = read_profile_info(reader);
    if (!profile) {
      return Expected<ClientboundPlayerProfileInfoBatch, ProtocolError>::failure(profile.error());
    }
    batch.profiles.push_back(std::move(profile.value()));
  }
  if (auto result = require_finished(reader, "player profile info batch"); !result) {
    return Expected<ClientboundPlayerProfileInfoBatch, ProtocolError>::failure(result.error());
  }
  return Expected<ClientboundPlayerProfileInfoBatch, ProtocolError>::success(std::move(batch));
}

std::vector<u8> serialize_skin_request(const ServerboundSkinRequest& request) {
  MessageWriter writer;
  writer.write<u32>(request.request_id);
  writer.write_string(request.skin_hash);
  return std::move(writer).take_bytes();
}

Expected<ServerboundSkinRequest, ProtocolError> deserialize_skin_request(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ServerboundSkinRequest request;
  auto request_id = reader.read<u32>();
  auto hash = reader.read_string();
  if (!request_id || !hash) {
    return Expected<ServerboundSkinRequest, ProtocolError>::failure(
        ProtocolError{"failed to read skin request"});
  }
  if (auto result = validate_required_hash(hash.value(), "skin request hash"); !result) {
    return Expected<ServerboundSkinRequest, ProtocolError>::failure(result.error());
  }
  if (auto result = require_finished(reader, "skin request"); !result) {
    return Expected<ServerboundSkinRequest, ProtocolError>::failure(result.error());
  }
  request.request_id = request_id.value();
  request.skin_hash = std::move(hash.value());
  return Expected<ServerboundSkinRequest, ProtocolError>::success(std::move(request));
}

std::vector<u8> serialize_skin_request_batch(const ServerboundSkinRequestBatch& batch) {
  MessageWriter writer;
  writer.write<u32>(static_cast<u32>(batch.requests.size()));
  for (const SkinRequestEntry& request : batch.requests) {
    writer.write<u32>(request.request_id);
    writer.write_string(request.skin_hash);
  }
  return std::move(writer).take_bytes();
}

Expected<ServerboundSkinRequestBatch, ProtocolError> deserialize_skin_request_batch(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto count = reader.read<u32>();
  if (!count) {
    return Expected<ServerboundSkinRequestBatch, ProtocolError>::failure(count.error());
  }
  if (count.value() > MaxSkinRequestBatchEntries) {
    return Expected<ServerboundSkinRequestBatch, ProtocolError>::failure(
        ProtocolError{"too many skin request entries"});
  }
  ServerboundSkinRequestBatch batch;
  batch.requests.reserve(count.value());
  for (u32 index = 0; index < count.value(); ++index) {
    auto request_id = reader.read<u32>();
    auto hash = reader.read_string();
    if (!request_id || !hash) {
      return Expected<ServerboundSkinRequestBatch, ProtocolError>::failure(
          ProtocolError{"failed to read skin request batch entry"});
    }
    if (auto result = validate_required_hash(hash.value(), "skin request hash"); !result) {
      return Expected<ServerboundSkinRequestBatch, ProtocolError>::failure(result.error());
    }
    batch.requests.push_back({request_id.value(), std::move(hash.value())});
  }
  if (auto result = require_finished(reader, "skin request batch"); !result) {
    return Expected<ServerboundSkinRequestBatch, ProtocolError>::failure(result.error());
  }
  return Expected<ServerboundSkinRequestBatch, ProtocolError>::success(std::move(batch));
}

std::vector<u8> serialize_skin_data(const ClientboundSkinData& data) {
  MessageWriter writer;
  writer.write<u32>(data.request_id);
  writer.write_string(data.skin_hash);
  writer.write<SkinImageFormat>(data.format);
  writer.write<u16>(data.width);
  writer.write<u16>(data.height);
  write_blob(writer, data.compressed_image_bytes);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSkinData, ProtocolError> deserialize_skin_data(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ClientboundSkinData data;
  auto request_id = reader.read<u32>();
  auto hash = reader.read_string();
  auto format = read_skin_format(reader);
  auto width = reader.read<u16>();
  auto height = reader.read<u16>();
  if (!request_id || !hash || !format || !width || !height) {
    return Expected<ClientboundSkinData, ProtocolError>::failure(
        ProtocolError{"failed to read skin data"});
  }
  if (auto result = validate_required_hash(hash.value(), "skin data hash"); !result) {
    return Expected<ClientboundSkinData, ProtocolError>::failure(result.error());
  }
  auto blob = read_blob(reader, MaxSkinImagePayloadBytes, "skin data payload");
  if (!blob) {
    return Expected<ClientboundSkinData, ProtocolError>::failure(blob.error());
  }
  if (auto result = require_finished(reader, "skin data"); !result) {
    return Expected<ClientboundSkinData, ProtocolError>::failure(result.error());
  }
  data.request_id = request_id.value();
  data.skin_hash = std::move(hash.value());
  data.format = format.value();
  data.width = width.value();
  data.height = height.value();
  data.compressed_image_bytes = std::move(blob.value());
  return Expected<ClientboundSkinData, ProtocolError>::success(std::move(data));
}

std::vector<u8> serialize_skin_data_begin_chunked(
    const ClientboundSkinDataBeginChunked& begin) {
  MessageWriter writer;
  writer.write<u32>(begin.request_id);
  writer.write_string(begin.skin_hash);
  writer.write<SkinImageFormat>(begin.format);
  writer.write<u16>(begin.width);
  writer.write<u16>(begin.height);
  writer.write<u32>(begin.total_bytes);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSkinDataBeginChunked, ProtocolError> deserialize_skin_data_begin_chunked(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ClientboundSkinDataBeginChunked begin;
  auto request_id = reader.read<u32>();
  auto hash = reader.read_string();
  auto format = read_skin_format(reader);
  auto width = reader.read<u16>();
  auto height = reader.read<u16>();
  auto total_bytes = reader.read<u32>();
  if (!request_id || !hash || !format || !width || !height || !total_bytes) {
    return Expected<ClientboundSkinDataBeginChunked, ProtocolError>::failure(
        ProtocolError{"failed to read chunked skin data begin"});
  }
  if (auto result = validate_required_hash(hash.value(), "skin data hash"); !result) {
    return Expected<ClientboundSkinDataBeginChunked, ProtocolError>::failure(result.error());
  }
  if (total_bytes.value() > MaxSkinImagePayloadBytes) {
    return Expected<ClientboundSkinDataBeginChunked, ProtocolError>::failure(
        ProtocolError{"chunked skin data total exceeds protocol limit"});
  }
  if (auto result = require_finished(reader, "chunked skin data begin"); !result) {
    return Expected<ClientboundSkinDataBeginChunked, ProtocolError>::failure(result.error());
  }
  begin.request_id = request_id.value();
  begin.skin_hash = std::move(hash.value());
  begin.format = format.value();
  begin.width = width.value();
  begin.height = height.value();
  begin.total_bytes = total_bytes.value();
  return Expected<ClientboundSkinDataBeginChunked, ProtocolError>::success(std::move(begin));
}

std::vector<u8> serialize_skin_data_chunk(const ClientboundSkinDataChunk& chunk) {
  MessageWriter writer;
  writer.write<u32>(chunk.request_id);
  write_blob(writer, chunk.chunk);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSkinDataChunk, ProtocolError> deserialize_skin_data_chunk(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ClientboundSkinDataChunk chunk;
  auto request_id = reader.read<u32>();
  if (!request_id) {
    return Expected<ClientboundSkinDataChunk, ProtocolError>::failure(request_id.error());
  }
  auto blob = read_blob(reader, MaxSkinImagePayloadBytes, "skin data chunk");
  if (!blob) {
    return Expected<ClientboundSkinDataChunk, ProtocolError>::failure(blob.error());
  }
  if (auto result = require_finished(reader, "skin data chunk"); !result) {
    return Expected<ClientboundSkinDataChunk, ProtocolError>::failure(result.error());
  }
  chunk.request_id = request_id.value();
  chunk.chunk = std::move(blob.value());
  return Expected<ClientboundSkinDataChunk, ProtocolError>::success(std::move(chunk));
}

std::vector<u8> serialize_skin_data_end_chunked(
    const ClientboundSkinDataEndChunked& end) {
  MessageWriter writer;
  writer.write<u32>(end.request_id);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSkinDataEndChunked, ProtocolError> deserialize_skin_data_end_chunked(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto request_id = reader.read<u32>();
  if (!request_id) {
    return Expected<ClientboundSkinDataEndChunked, ProtocolError>::failure(request_id.error());
  }
  if (auto result = require_finished(reader, "chunked skin data end"); !result) {
    return Expected<ClientboundSkinDataEndChunked, ProtocolError>::failure(result.error());
  }
  return Expected<ClientboundSkinDataEndChunked, ProtocolError>::success({request_id.value()});
}

std::vector<u8> serialize_skin_request_deny(const ClientboundSkinRequestDeny& deny) {
  MessageWriter writer;
  writer.write<u32>(deny.request_id);
  writer.write_string(deny.skin_hash);
  writer.write<SkinRequestDenyReason>(deny.reason);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSkinRequestDeny, ProtocolError> deserialize_skin_request_deny(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ClientboundSkinRequestDeny deny;
  auto request_id = reader.read<u32>();
  auto hash = reader.read_string();
  auto reason = read_request_deny_reason(reader);
  if (!request_id || !hash || !reason) {
    return Expected<ClientboundSkinRequestDeny, ProtocolError>::failure(
        ProtocolError{"failed to read skin request deny"});
  }
  if (!valid_hash_or_empty(hash.value())) {
    return Expected<ClientboundSkinRequestDeny, ProtocolError>::failure(
        ProtocolError{"invalid denied skin hash"});
  }
  if (auto result = require_finished(reader, "skin request deny"); !result) {
    return Expected<ClientboundSkinRequestDeny, ProtocolError>::failure(result.error());
  }
  deny.request_id = request_id.value();
  deny.skin_hash = std::move(hash.value());
  deny.reason = reason.value();
  return Expected<ClientboundSkinRequestDeny, ProtocolError>::success(std::move(deny));
}

std::vector<u8> serialize_skin_upload_begin(const ServerboundSkinUploadBegin& begin) {
  MessageWriter writer;
  writer.write<SkinImageFormat>(begin.format);
  writer.write_string(begin.client_skin_hash_hint);
  writer.write<u16>(begin.width);
  writer.write<u16>(begin.height);
  writer.write_bool(begin.slim_model);
  writer.write<u32>(begin.total_upload_bytes);
  return std::move(writer).take_bytes();
}

Expected<ServerboundSkinUploadBegin, ProtocolError> deserialize_skin_upload_begin(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ServerboundSkinUploadBegin begin;
  auto format = read_skin_format(reader);
  auto hint = reader.read_string();
  auto width = reader.read<u16>();
  auto height = reader.read<u16>();
  auto slim_model = reader.read_bool();
  auto total_bytes = reader.read<u32>();
  if (!format || !hint || !width || !height || !slim_model || !total_bytes) {
    return Expected<ServerboundSkinUploadBegin, ProtocolError>::failure(
        ProtocolError{"failed to read skin upload begin"});
  }
  if (!valid_hash_or_empty(hint.value())) {
    return Expected<ServerboundSkinUploadBegin, ProtocolError>::failure(
        ProtocolError{"invalid skin upload hash hint"});
  }
  if (auto result = require_finished(reader, "skin upload begin"); !result) {
    return Expected<ServerboundSkinUploadBegin, ProtocolError>::failure(result.error());
  }
  begin.format = format.value();
  begin.client_skin_hash_hint = std::move(hint.value());
  begin.width = width.value();
  begin.height = height.value();
  begin.slim_model = slim_model.value();
  begin.total_upload_bytes = total_bytes.value();
  return Expected<ServerboundSkinUploadBegin, ProtocolError>::success(std::move(begin));
}

std::vector<u8> serialize_skin_upload_accept(const ClientboundSkinUploadAccept& accept) {
  MessageWriter writer;
  writer.write<u32>(accept.upload_id);
  writer.write<SkinUploadMode>(accept.upload_mode);
  writer.write<u32>(accept.max_chunk_bytes);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSkinUploadAccept, ProtocolError> deserialize_skin_upload_accept(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ClientboundSkinUploadAccept accept;
  auto upload_id = reader.read<u32>();
  auto mode = read_upload_mode(reader);
  auto max_chunk_bytes = reader.read<u32>();
  if (!upload_id || !mode || !max_chunk_bytes) {
    return Expected<ClientboundSkinUploadAccept, ProtocolError>::failure(
        ProtocolError{"failed to read skin upload accept"});
  }
  if (auto result = require_finished(reader, "skin upload accept"); !result) {
    return Expected<ClientboundSkinUploadAccept, ProtocolError>::failure(result.error());
  }
  accept.upload_id = upload_id.value();
  accept.upload_mode = mode.value();
  accept.max_chunk_bytes = max_chunk_bytes.value();
  return Expected<ClientboundSkinUploadAccept, ProtocolError>::success(accept);
}

std::vector<u8> serialize_skin_upload_deny(const ClientboundSkinUploadDeny& deny) {
  MessageWriter writer;
  writer.write<SkinUploadDenyReason>(deny.reason);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSkinUploadDeny, ProtocolError> deserialize_skin_upload_deny(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto reason = read_upload_deny_reason(reader);
  if (!reason) {
    return Expected<ClientboundSkinUploadDeny, ProtocolError>::failure(reason.error());
  }
  if (auto result = require_finished(reader, "skin upload deny"); !result) {
    return Expected<ClientboundSkinUploadDeny, ProtocolError>::failure(result.error());
  }
  return Expected<ClientboundSkinUploadDeny, ProtocolError>::success({reason.value()});
}

std::vector<u8> serialize_skin_upload_data(const ServerboundSkinUploadData& data) {
  MessageWriter writer;
  writer.write<u32>(data.upload_id);
  write_blob(writer, data.compressed_image_bytes);
  return std::move(writer).take_bytes();
}

Expected<ServerboundSkinUploadData, ProtocolError> deserialize_skin_upload_data(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ServerboundSkinUploadData data;
  auto upload_id = reader.read<u32>();
  if (!upload_id) {
    return Expected<ServerboundSkinUploadData, ProtocolError>::failure(upload_id.error());
  }
  auto blob = read_blob(reader, MaxSkinImagePayloadBytes, "skin upload data");
  if (!blob) {
    return Expected<ServerboundSkinUploadData, ProtocolError>::failure(blob.error());
  }
  if (auto result = require_finished(reader, "skin upload data"); !result) {
    return Expected<ServerboundSkinUploadData, ProtocolError>::failure(result.error());
  }
  data.upload_id = upload_id.value();
  data.compressed_image_bytes = std::move(blob.value());
  return Expected<ServerboundSkinUploadData, ProtocolError>::success(std::move(data));
}

std::vector<u8> serialize_skin_upload_chunk(const ServerboundSkinUploadChunk& chunk) {
  MessageWriter writer;
  writer.write<u32>(chunk.upload_id);
  write_blob(writer, chunk.image_chunk_bytes);
  return std::move(writer).take_bytes();
}

Expected<ServerboundSkinUploadChunk, ProtocolError> deserialize_skin_upload_chunk(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ServerboundSkinUploadChunk chunk;
  auto upload_id = reader.read<u32>();
  if (!upload_id) {
    return Expected<ServerboundSkinUploadChunk, ProtocolError>::failure(upload_id.error());
  }
  auto blob = read_blob(reader, MaxSkinImagePayloadBytes, "skin upload chunk");
  if (!blob) {
    return Expected<ServerboundSkinUploadChunk, ProtocolError>::failure(blob.error());
  }
  if (auto result = require_finished(reader, "skin upload chunk"); !result) {
    return Expected<ServerboundSkinUploadChunk, ProtocolError>::failure(result.error());
  }
  chunk.upload_id = upload_id.value();
  chunk.image_chunk_bytes = std::move(blob.value());
  return Expected<ServerboundSkinUploadChunk, ProtocolError>::success(std::move(chunk));
}

std::vector<u8> serialize_skin_upload_end(const ServerboundSkinUploadEnd& end) {
  MessageWriter writer;
  writer.write<u32>(end.upload_id);
  return std::move(writer).take_bytes();
}

Expected<ServerboundSkinUploadEnd, ProtocolError> deserialize_skin_upload_end(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto upload_id = reader.read<u32>();
  if (!upload_id) {
    return Expected<ServerboundSkinUploadEnd, ProtocolError>::failure(upload_id.error());
  }
  if (auto result = require_finished(reader, "skin upload end"); !result) {
    return Expected<ServerboundSkinUploadEnd, ProtocolError>::failure(result.error());
  }
  return Expected<ServerboundSkinUploadEnd, ProtocolError>::success({upload_id.value()});
}

std::vector<u8> serialize_skin_upload_complete(
    const ClientboundSkinUploadComplete& complete) {
  MessageWriter writer;
  writer.write<u32>(complete.upload_id);
  write_skin_descriptor(writer, complete.skin);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSkinUploadComplete, ProtocolError> deserialize_skin_upload_complete(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto upload_id = reader.read<u32>();
  if (!upload_id) {
    return Expected<ClientboundSkinUploadComplete, ProtocolError>::failure(upload_id.error());
  }
  auto skin = read_skin_descriptor(reader);
  if (!skin) {
    return Expected<ClientboundSkinUploadComplete, ProtocolError>::failure(skin.error());
  }
  if (auto result = require_finished(reader, "skin upload complete"); !result) {
    return Expected<ClientboundSkinUploadComplete, ProtocolError>::failure(result.error());
  }
  return Expected<ClientboundSkinUploadComplete, ProtocolError>::success(
      {upload_id.value(), std::move(skin.value())});
}

std::vector<u8> serialize_skin_upload_failed(const ClientboundSkinUploadFailed& failed) {
  MessageWriter writer;
  writer.write<u32>(failed.upload_id);
  writer.write<SkinUploadDenyReason>(failed.reason);
  return std::move(writer).take_bytes();
}

Expected<ClientboundSkinUploadFailed, ProtocolError> deserialize_skin_upload_failed(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto upload_id = reader.read<u32>();
  auto reason = read_upload_deny_reason(reader);
  if (!upload_id || !reason) {
    return Expected<ClientboundSkinUploadFailed, ProtocolError>::failure(
        ProtocolError{"failed to read skin upload failed"});
  }
  if (auto result = require_finished(reader, "skin upload failed"); !result) {
    return Expected<ClientboundSkinUploadFailed, ProtocolError>::failure(result.error());
  }
  return Expected<ClientboundSkinUploadFailed, ProtocolError>::success(
      {upload_id.value(), reason.value()});
}

} // namespace arv::protocol
