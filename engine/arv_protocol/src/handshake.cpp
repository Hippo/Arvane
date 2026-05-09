#include "arv/protocol/handshake.hpp"

#include <algorithm>
#include <utility>

namespace arv::protocol {
namespace {

void write_named_hashes(MessageWriter& writer, const std::vector<NamedHash>& hashes) {
  writer.write<u32>(static_cast<u32>(hashes.size()));
  for (const NamedHash& hash : hashes) {
    writer.write_string(hash.name);
    writer.write<u64>(hash.hash);
  }
}

Expected<std::vector<NamedHash>, ProtocolError> read_named_hashes(MessageReader& reader) {
  auto count = reader.read<u32>();
  if (!count) {
    return Expected<std::vector<NamedHash>, ProtocolError>::failure(count.error());
  }
  if (count.value() > MaxHandshakeHashEntries) {
    return Expected<std::vector<NamedHash>, ProtocolError>::failure(
        ProtocolError{"too many handshake hash entries"});
  }

  std::vector<NamedHash> hashes;
  hashes.reserve(count.value());
  for (u32 i = 0; i < count.value(); ++i) {
    auto name = reader.read_string();
    auto hash = reader.read<u64>();
    if (!name || !hash) {
      return Expected<std::vector<NamedHash>, ProtocolError>::failure(
          ProtocolError{"failed to read handshake hash entry"});
    }
    hashes.push_back({std::move(name.value()), hash.value()});
  }
  return Expected<std::vector<NamedHash>, ProtocolError>::success(std::move(hashes));
}

bool contains_compression_mode(
    std::span<const CompressionMode> supported,
    CompressionMode selected) noexcept {
  return std::find(supported.begin(), supported.end(), selected) != supported.end();
}

bool known_hashes_match(
    std::span<const NamedHash> authoritative,
    std::span<const NamedHash> known_by_client) noexcept {
  for (const NamedHash& expected : authoritative) {
    const auto found = std::find_if(
        known_by_client.begin(),
        known_by_client.end(),
        [&](const NamedHash& known) {
          return known.name == expected.name && known.hash == expected.hash;
        });
    if (found == known_by_client.end()) {
      return false;
    }
  }
  return true;
}

} // namespace

std::vector<u8> serialize_serverbound_hello(const ServerboundHello& hello) {
  MessageWriter writer;
  writer.write<u16>(hello.protocol_version);
  writer.write_string(hello.client_build_id);
  writer.write_string(hello.client_name_or_brand);
  writer.write<u32>(static_cast<u32>(hello.supported_compression_modes.size()));
  for (CompressionMode mode : hello.supported_compression_modes) {
    writer.write<CompressionMode>(mode);
  }
  writer.write<ProtocolFeatureBits>(hello.supported_protocol_features);
  write_named_hashes(writer, hello.registry_hashes_known_by_client);
  write_named_hashes(writer, hello.asset_pack_hashes_known_by_client);
  writer.write_string(hello.requested_username_or_profile_name);
  writer.write_string(hello.session_token_or_placeholder);
  writer.write_bool(hello.auth_begin.has_value());
  if (hello.auth_begin.has_value()) {
    writer.write_bytes(hello.auth_begin->public_key.bytes);
    writer.write_string(hello.auth_begin->display_name);
    writer.write_string(hello.auth_begin->claimed_profile_id.string());
  }
  return std::move(writer).take_bytes();
}

Expected<ServerboundHello, ProtocolError> deserialize_serverbound_hello(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ServerboundHello hello;

  auto protocol_version = reader.read<u16>();
  auto client_build_id = reader.read_string();
  auto client_name = reader.read_string();
  auto compression_count = reader.read<u32>();
  if (!protocol_version || !client_build_id || !client_name || !compression_count) {
    return Expected<ServerboundHello, ProtocolError>::failure(ProtocolError{"failed to read client hello header"});
  }
  if (compression_count.value() > MaxHandshakeHashEntries) {
    return Expected<ServerboundHello, ProtocolError>::failure(ProtocolError{"too many compression modes"});
  }

  hello.protocol_version = protocol_version.value();
  hello.client_build_id = std::move(client_build_id.value());
  hello.client_name_or_brand = std::move(client_name.value());
  hello.supported_compression_modes.clear();
  hello.supported_compression_modes.reserve(compression_count.value());
  for (u32 i = 0; i < compression_count.value(); ++i) {
    auto mode = reader.read<CompressionMode>();
    if (!mode) {
      return Expected<ServerboundHello, ProtocolError>::failure(mode.error());
    }
    hello.supported_compression_modes.push_back(mode.value());
  }

  auto features = reader.read<ProtocolFeatureBits>();
  if (!features) {
    return Expected<ServerboundHello, ProtocolError>::failure(features.error());
  }
  hello.supported_protocol_features = features.value();

  auto registry_hashes = read_named_hashes(reader);
  auto asset_hashes = read_named_hashes(reader);
  auto username = reader.read_string();
  auto token = reader.read_string();
  if (!registry_hashes || !asset_hashes || !username || !token) {
    return Expected<ServerboundHello, ProtocolError>::failure(ProtocolError{"failed to read client hello body"});
  }

  hello.registry_hashes_known_by_client = std::move(registry_hashes.value());
  hello.asset_pack_hashes_known_by_client = std::move(asset_hashes.value());
  hello.requested_username_or_profile_name = std::move(username.value());
  hello.session_token_or_placeholder = std::move(token.value());

  if (reader.remaining() != 0) {
    auto has_auth = reader.read_bool();
    if (!has_auth) {
      return Expected<ServerboundHello, ProtocolError>::failure(
          ProtocolError{"failed to read client auth flag"});
    }
    if (has_auth.value()) {
      ServerboundAuthBegin auth;
      if (auto result = reader.read_bytes(auth.public_key.bytes); !result) {
        return Expected<ServerboundHello, ProtocolError>::failure(result.error());
      }
      auto display_name = reader.read_string();
      auto claimed_profile_id = reader.read_string();
      if (!display_name || !claimed_profile_id) {
        return Expected<ServerboundHello, ProtocolError>::failure(
            ProtocolError{"failed to read client auth begin"});
      }
      auth.display_name = std::move(display_name.value());
      auto parsed_profile_id = crypto::parse_player_profile_id(claimed_profile_id.value());
      if (!parsed_profile_id) {
        return Expected<ServerboundHello, ProtocolError>::failure(
            ProtocolError{parsed_profile_id.error().message});
      }
      auth.claimed_profile_id = parsed_profile_id.value();
      hello.auth_begin = std::move(auth);
    }
  }

  if (reader.remaining() != 0) {
    return Expected<ServerboundHello, ProtocolError>::failure(ProtocolError{"trailing bytes in client hello"});
  }

  return Expected<ServerboundHello, ProtocolError>::success(std::move(hello));
}

std::vector<u8> serialize_clientbound_hello(const ClientboundHello& hello) {
  MessageWriter writer;
  writer.write_bool(hello.accepted);
  writer.write_string(hello.reject_reason);
  writer.write<u16>(hello.protocol_version);
  writer.write_string(hello.server_build_id);
  writer.write_string(hello.server_name);
  writer.write<CompressionMode>(hello.selected_compression_mode);
  writer.write<ProtocolFeatureBits>(hello.enabled_protocol_features);
  write_named_hashes(writer, hello.authoritative_registry_hashes);
  write_named_hashes(writer, hello.authoritative_asset_pack_hashes);
  writer.write<u64>(hello.assigned_session_id.value);
  writer.write<u64>(hello.assigned_player_id.value);
  writer.write<u32>(hello.initial_world_id.value);
  writer.write_bool(hello.required_followup_sync);
  return std::move(writer).take_bytes();
}

Expected<ClientboundHello, ProtocolError> deserialize_clientbound_hello(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ClientboundHello hello;

  auto accepted = reader.read_bool();
  auto reject_reason = reader.read_string();
  auto protocol_version = reader.read<u16>();
  auto server_build_id = reader.read_string();
  auto server_name = reader.read_string();
  auto compression = reader.read<CompressionMode>();
  auto features = reader.read<ProtocolFeatureBits>();
  if (!accepted || !reject_reason || !protocol_version || !server_build_id ||
      !server_name || !compression || !features) {
    return Expected<ClientboundHello, ProtocolError>::failure(ProtocolError{"failed to read server hello header"});
  }

  auto registry_hashes = read_named_hashes(reader);
  auto asset_hashes = read_named_hashes(reader);
  auto session_id = reader.read<u64>();
  auto player_id = reader.read<u64>();
  auto world_id = reader.read<u32>();
  auto required_sync = reader.read_bool();
  if (!registry_hashes || !asset_hashes || !session_id || !player_id || !world_id || !required_sync) {
    return Expected<ClientboundHello, ProtocolError>::failure(ProtocolError{"failed to read server hello body"});
  }

  hello.accepted = accepted.value();
  hello.reject_reason = std::move(reject_reason.value());
  hello.protocol_version = protocol_version.value();
  hello.server_build_id = std::move(server_build_id.value());
  hello.server_name = std::move(server_name.value());
  hello.selected_compression_mode = compression.value();
  hello.enabled_protocol_features = features.value();
  hello.authoritative_registry_hashes = std::move(registry_hashes.value());
  hello.authoritative_asset_pack_hashes = std::move(asset_hashes.value());
  hello.assigned_session_id = SessionID{session_id.value()};
  hello.assigned_player_id = PlayerID{player_id.value()};
  hello.initial_world_id = WorldID{world_id.value()};
  hello.required_followup_sync = required_sync.value();

  if (reader.remaining() != 0) {
    return Expected<ClientboundHello, ProtocolError>::failure(ProtocolError{"trailing bytes in server hello"});
  }

  return Expected<ClientboundHello, ProtocolError>::success(std::move(hello));
}

ClientboundHello make_clientbound_hello(const ServerboundHello& client, const HandshakePolicy& policy) {
  ClientboundHello hello;
  hello.protocol_version = policy.protocol_version;
  hello.server_build_id = policy.server_build_id;
  hello.server_name = policy.server_name;
  hello.selected_compression_mode = policy.selected_compression_mode;
  hello.enabled_protocol_features = policy.enabled_protocol_features & client.supported_protocol_features;
  hello.authoritative_registry_hashes = policy.authoritative_registry_hashes;
  hello.authoritative_asset_pack_hashes = policy.authoritative_asset_pack_hashes;
  hello.assigned_session_id = policy.assigned_session_id;
  hello.assigned_player_id = policy.assigned_player_id;
  hello.initial_world_id = policy.initial_world_id;

  if (client.protocol_version != policy.protocol_version) {
    hello.accepted = false;
    hello.reject_reason = "unsupported protocol version";
    return hello;
  }

  if (!contains_compression_mode(
          client.supported_compression_modes,
          policy.selected_compression_mode)) {
    hello.accepted = false;
    hello.reject_reason = "unsupported compression mode";
    return hello;
  }

  if (!known_hashes_match(
          policy.authoritative_registry_hashes,
          client.registry_hashes_known_by_client)) {
    hello.accepted = false;
    hello.reject_reason = "registry sync required";
    hello.required_followup_sync = true;
    return hello;
  }

  if (!known_hashes_match(
          policy.authoritative_asset_pack_hashes,
          client.asset_pack_hashes_known_by_client)) {
    hello.accepted = false;
    hello.reject_reason = "asset pack sync required";
    hello.required_followup_sync = true;
    return hello;
  }

  hello.accepted = true;
  return hello;
}

} // namespace arv::protocol
