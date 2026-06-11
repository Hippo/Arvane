#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/message_writer.hpp"

#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

inline constexpr u32 PackSyncProtocolVersion = 1U;
inline constexpr u32 PackSyncDefaultChunkSize = 64U * 1024U;

struct PackBundleDescriptor {
  std::string pack_id{};
  std::string pack_name{};
  std::string pack_version{};
  std::string bundle_hash{};
  u64 compressed_size{};
  u64 uncompressed_size{};
};

enum class PackSyncStatus : u8 {
  Accepted = 0,
  RejectedByUser = 1,
  AlreadyCached = 2,
  NeedsDownload = 3,
  DownloadFailed = 4,
  VerificationFailed = 5,
  Loaded = 6,
};

struct ClientboundPackManifest {
  u32 pack_sync_protocol_version{PackSyncProtocolVersion};
  std::vector<PackBundleDescriptor> required_bundles{};
  std::string registry_hash{};
  u64 max_bundle_size{};
  u64 total_download_size{};
};

struct ServerboundPackSyncStatus {
  PackSyncStatus status{PackSyncStatus::Accepted};
  std::vector<std::string> already_cached_bundle_hashes{};
  std::vector<std::string> needed_bundle_hashes{};
  std::vector<std::string> failed_bundle_hashes{};
  std::string failure_reason{};
};

struct ServerboundPackRequest {
  std::string bundle_hash{};
};

struct ClientboundPackDataChunk {
  std::string bundle_hash{};
  u32 chunk_index{};
  u32 chunk_count{};
  u64 byte_offset{};
  std::vector<u8> bytes{};
};

struct ClientboundRegistrySnapshot {
  std::string registry_hash{};
  std::string registry_snapshot_payload{};
};

struct ServerboundPackReady {
  std::string registry_hash{};
  std::vector<std::string> loaded_bundle_hashes{};
};

std::vector<u8> serialize_clientbound_pack_manifest(const ClientboundPackManifest& manifest);
Expected<ClientboundPackManifest, ProtocolError> deserialize_clientbound_pack_manifest(std::span<const u8> bytes);

std::vector<u8> serialize_serverbound_pack_sync_status(const ServerboundPackSyncStatus& status);
Expected<ServerboundPackSyncStatus, ProtocolError> deserialize_serverbound_pack_sync_status(std::span<const u8> bytes);

std::vector<u8> serialize_serverbound_pack_request(const ServerboundPackRequest& request);
Expected<ServerboundPackRequest, ProtocolError> deserialize_serverbound_pack_request(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_pack_data_chunk(const ClientboundPackDataChunk& chunk);
Expected<ClientboundPackDataChunk, ProtocolError> deserialize_clientbound_pack_data_chunk(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_registry_snapshot(const ClientboundRegistrySnapshot& snapshot);
Expected<ClientboundRegistrySnapshot, ProtocolError> deserialize_clientbound_registry_snapshot(std::span<const u8> bytes);

std::vector<u8> serialize_serverbound_pack_ready(const ServerboundPackReady& ready);
Expected<ServerboundPackReady, ProtocolError> deserialize_serverbound_pack_ready(std::span<const u8> bytes);

} // namespace arv::protocol
