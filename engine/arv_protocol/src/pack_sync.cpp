#include "arv/protocol/pack_sync.hpp"

#include "arv/protocol/message_primitives.hpp"

#include <utility>

namespace arv::protocol {
namespace {

void write_string_list(MessageWriter& writer, std::span<const std::string> values) {
  writer.write<u16>(static_cast<u16>(values.size()));
  for (const std::string& value : values) {
    writer.write_string(value);
  }
}

Expected<std::vector<std::string>, ProtocolError> read_string_list(MessageReader& reader) {
  auto count = reader.read<u16>();
  if (!count) {
    return Expected<std::vector<std::string>, ProtocolError>::failure(count.error());
  }
  std::vector<std::string> values;
  values.reserve(count.value());
  for (u16 index = 0; index < count.value(); ++index) {
    auto value = reader.read_string();
    if (!value) {
      return Expected<std::vector<std::string>, ProtocolError>::failure(value.error());
    }
    values.push_back(std::move(value.value()));
  }
  return Expected<std::vector<std::string>, ProtocolError>::success(std::move(values));
}

void write_bundle_descriptor(MessageWriter& writer, const PackBundleDescriptor& bundle) {
  writer.write_string(bundle.pack_id);
  writer.write_string(bundle.pack_name);
  writer.write_string(bundle.pack_version);
  writer.write_string(bundle.bundle_hash);
  writer.write<u64>(bundle.compressed_size);
  writer.write<u64>(bundle.uncompressed_size);
}

Expected<PackBundleDescriptor, ProtocolError> read_bundle_descriptor(MessageReader& reader) {
  auto pack_id = reader.read_string();
  auto pack_name = reader.read_string();
  auto pack_version = reader.read_string();
  auto bundle_hash = reader.read_string();
  auto compressed_size = reader.read<u64>();
  auto uncompressed_size = reader.read<u64>();
  if (!pack_id || !pack_name || !pack_version || !bundle_hash ||
      !compressed_size || !uncompressed_size) {
    return Expected<PackBundleDescriptor, ProtocolError>::failure(
        ProtocolError{"failed to decode pack bundle descriptor"});
  }
  return Expected<PackBundleDescriptor, ProtocolError>::success({
      std::move(pack_id.value()),
      std::move(pack_name.value()),
      std::move(pack_version.value()),
      std::move(bundle_hash.value()),
      compressed_size.value(),
      uncompressed_size.value()});
}

void write_byte_vector(MessageWriter& writer, std::span<const u8> bytes) {
  writer.write<u32>(static_cast<u32>(bytes.size()));
  writer.write_bytes(bytes);
}

Expected<std::vector<u8>, ProtocolError> read_byte_vector(MessageReader& reader) {
  auto size = reader.read<u32>();
  if (!size) {
    return Expected<std::vector<u8>, ProtocolError>::failure(size.error());
  }
  std::vector<u8> bytes(size.value());
  auto read = reader.read_bytes(bytes);
  if (!read) {
    return Expected<std::vector<u8>, ProtocolError>::failure(read.error());
  }
  return Expected<std::vector<u8>, ProtocolError>::success(std::move(bytes));
}

} // namespace

std::vector<u8> serialize_clientbound_pack_manifest(const ClientboundPackManifest& manifest) {
  MessageWriter writer;
  writer.write<u32>(manifest.pack_sync_protocol_version);
  writer.write<u16>(static_cast<u16>(manifest.required_bundles.size()));
  for (const PackBundleDescriptor& bundle : manifest.required_bundles) {
    write_bundle_descriptor(writer, bundle);
  }
  writer.write_string(manifest.registry_hash);
  writer.write<u64>(manifest.max_bundle_size);
  writer.write<u64>(manifest.total_download_size);
  return std::move(writer).take_bytes();
}

Expected<ClientboundPackManifest, ProtocolError> deserialize_clientbound_pack_manifest(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto version = reader.read<u32>();
  auto bundle_count = reader.read<u16>();
  if (!version || !bundle_count) {
    return Expected<ClientboundPackManifest, ProtocolError>::failure(
        ProtocolError{"failed to decode pack manifest"});
  }
  ClientboundPackManifest manifest;
  manifest.pack_sync_protocol_version = version.value();
  manifest.required_bundles.reserve(bundle_count.value());
  for (u16 index = 0; index < bundle_count.value(); ++index) {
    auto bundle = read_bundle_descriptor(reader);
    if (!bundle) {
      return Expected<ClientboundPackManifest, ProtocolError>::failure(bundle.error());
    }
    manifest.required_bundles.push_back(std::move(bundle.value()));
  }
  auto registry_hash = reader.read_string();
  auto max_bundle_size = reader.read<u64>();
  auto total_download_size = reader.read<u64>();
  if (!registry_hash || !max_bundle_size || !total_download_size) {
    return Expected<ClientboundPackManifest, ProtocolError>::failure(
        ProtocolError{"failed to decode pack manifest"});
  }
  manifest.registry_hash = std::move(registry_hash.value());
  manifest.max_bundle_size = max_bundle_size.value();
  manifest.total_download_size = total_download_size.value();
  auto trailing = require_no_trailing_bytes(reader, "pack manifest");
  if (!trailing) {
    return Expected<ClientboundPackManifest, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundPackManifest, ProtocolError>::success(std::move(manifest));
}

std::vector<u8> serialize_serverbound_pack_sync_status(const ServerboundPackSyncStatus& status) {
  MessageWriter writer;
  writer.write<PackSyncStatus>(status.status);
  write_string_list(writer, status.already_cached_bundle_hashes);
  write_string_list(writer, status.needed_bundle_hashes);
  write_string_list(writer, status.failed_bundle_hashes);
  writer.write_string(status.failure_reason);
  return std::move(writer).take_bytes();
}

Expected<ServerboundPackSyncStatus, ProtocolError> deserialize_serverbound_pack_sync_status(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto status = reader.read<PackSyncStatus>();
  auto already_cached = read_string_list(reader);
  auto needed = read_string_list(reader);
  auto failed = read_string_list(reader);
  auto reason = reader.read_string();
  if (!status || !already_cached || !needed || !failed || !reason) {
    return Expected<ServerboundPackSyncStatus, ProtocolError>::failure(
        ProtocolError{"failed to decode pack sync status"});
  }
  auto trailing = require_no_trailing_bytes(reader, "pack sync status");
  if (!trailing) {
    return Expected<ServerboundPackSyncStatus, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundPackSyncStatus, ProtocolError>::success({
      status.value(),
      std::move(already_cached.value()),
      std::move(needed.value()),
      std::move(failed.value()),
      std::move(reason.value())});
}

std::vector<u8> serialize_serverbound_pack_request(const ServerboundPackRequest& request) {
  MessageWriter writer;
  writer.write_string(request.bundle_hash);
  return std::move(writer).take_bytes();
}

Expected<ServerboundPackRequest, ProtocolError> deserialize_serverbound_pack_request(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto hash = reader.read_string();
  if (!hash) {
    return Expected<ServerboundPackRequest, ProtocolError>::failure(
        ProtocolError{"failed to decode pack request"});
  }
  auto trailing = require_no_trailing_bytes(reader, "pack request");
  if (!trailing) {
    return Expected<ServerboundPackRequest, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundPackRequest, ProtocolError>::success({std::move(hash.value())});
}

std::vector<u8> serialize_clientbound_pack_data_chunk(const ClientboundPackDataChunk& chunk) {
  MessageWriter writer;
  writer.write_string(chunk.bundle_hash);
  writer.write<u32>(chunk.chunk_index);
  writer.write<u32>(chunk.chunk_count);
  writer.write<u64>(chunk.byte_offset);
  write_byte_vector(writer, chunk.bytes);
  return std::move(writer).take_bytes();
}

Expected<ClientboundPackDataChunk, ProtocolError> deserialize_clientbound_pack_data_chunk(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto hash = reader.read_string();
  auto index = reader.read<u32>();
  auto count = reader.read<u32>();
  auto offset = reader.read<u64>();
  auto payload = read_byte_vector(reader);
  if (!hash || !index || !count || !offset || !payload) {
    return Expected<ClientboundPackDataChunk, ProtocolError>::failure(
        ProtocolError{"failed to decode pack data chunk"});
  }
  auto trailing = require_no_trailing_bytes(reader, "pack data chunk");
  if (!trailing) {
    return Expected<ClientboundPackDataChunk, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundPackDataChunk, ProtocolError>::success({
      std::move(hash.value()),
      index.value(),
      count.value(),
      offset.value(),
      std::move(payload.value())});
}

std::vector<u8> serialize_clientbound_registry_snapshot(const ClientboundRegistrySnapshot& snapshot) {
  MessageWriter writer;
  writer.write_string(snapshot.registry_hash);
  const auto* first = reinterpret_cast<const u8*>(snapshot.registry_snapshot_payload.data());
  write_byte_vector(
      writer,
      std::span<const u8>{first, snapshot.registry_snapshot_payload.size()});
  return std::move(writer).take_bytes();
}

Expected<ClientboundRegistrySnapshot, ProtocolError> deserialize_clientbound_registry_snapshot(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto hash = reader.read_string();
  auto payload = read_byte_vector(reader);
  if (!hash || !payload) {
    return Expected<ClientboundRegistrySnapshot, ProtocolError>::failure(
        ProtocolError{"failed to decode registry snapshot"});
  }
  auto trailing = require_no_trailing_bytes(reader, "registry snapshot");
  if (!trailing) {
    return Expected<ClientboundRegistrySnapshot, ProtocolError>::failure(trailing.error());
  }
  std::string payload_text{
      reinterpret_cast<const char*>(payload.value().data()),
      payload.value().size()};
  return Expected<ClientboundRegistrySnapshot, ProtocolError>::success(
      {std::move(hash.value()), std::move(payload_text)});
}

std::vector<u8> serialize_serverbound_pack_ready(const ServerboundPackReady& ready) {
  MessageWriter writer;
  writer.write_string(ready.registry_hash);
  write_string_list(writer, ready.loaded_bundle_hashes);
  return std::move(writer).take_bytes();
}

Expected<ServerboundPackReady, ProtocolError> deserialize_serverbound_pack_ready(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto hash = reader.read_string();
  auto loaded = read_string_list(reader);
  if (!hash || !loaded) {
    return Expected<ServerboundPackReady, ProtocolError>::failure(
        ProtocolError{"failed to decode pack ready"});
  }
  auto trailing = require_no_trailing_bytes(reader, "pack ready");
  if (!trailing) {
    return Expected<ServerboundPackReady, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundPackReady, ProtocolError>::success(
      {std::move(hash.value()), std::move(loaded.value())});
}

} // namespace arv::protocol
