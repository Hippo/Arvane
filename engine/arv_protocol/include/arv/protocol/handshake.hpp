#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/protocol/protocol_version.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::protocol {

enum class CompressionMode : u16 {
  None = 0,
};

using ProtocolFeatureBits = u64;

struct NamedHash {
  std::string name{};
  u64 hash{};

  constexpr bool operator==(const NamedHash&) const = default;
};

struct ServerboundHello {
  u16 protocol_version{CurrentProtocolVersion};
  std::string client_build_id{"dev"};
  std::string client_name_or_brand{"arvane"};
  std::vector<CompressionMode> supported_compression_modes{CompressionMode::None};
  ProtocolFeatureBits supported_protocol_features{};
  std::vector<NamedHash> registry_hashes_known_by_client{};
  std::vector<NamedHash> asset_pack_hashes_known_by_client{};
  std::string requested_username_or_profile_name{"local_player"};
  std::string session_token_or_placeholder{"local"};
  std::optional<ServerboundAuthBegin> auth_begin{};
};

struct ClientboundHello {
  bool accepted{};
  std::string reject_reason{};
  u16 protocol_version{CurrentProtocolVersion};
  std::string server_build_id{"dev"};
  std::string server_name{"Arvane Server"};
  CompressionMode selected_compression_mode{CompressionMode::None};
  ProtocolFeatureBits enabled_protocol_features{};
  std::vector<NamedHash> authoritative_registry_hashes{};
  std::vector<NamedHash> authoritative_asset_pack_hashes{};
  SessionID assigned_session_id{};
  PlayerID assigned_player_id{};
  WorldID initial_world_id{};
  bool required_followup_sync{};
};

struct HandshakePolicy {
  u16 protocol_version{CurrentProtocolVersion};
  std::string server_build_id{"dev"};
  std::string server_name{"Arvane Server"};
  CompressionMode selected_compression_mode{CompressionMode::None};
  ProtocolFeatureBits enabled_protocol_features{};
  std::vector<NamedHash> authoritative_registry_hashes{};
  std::vector<NamedHash> authoritative_asset_pack_hashes{};
  SessionID assigned_session_id{};
  PlayerID assigned_player_id{};
  WorldID initial_world_id{};
};

inline constexpr u32 MaxHandshakeHashEntries = 64;

std::vector<u8> serialize_serverbound_hello(const ServerboundHello& hello);

Expected<ServerboundHello, ProtocolError> deserialize_serverbound_hello(std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_hello(const ClientboundHello& hello);

Expected<ClientboundHello, ProtocolError> deserialize_clientbound_hello(std::span<const u8> bytes);

ClientboundHello make_clientbound_hello(const ServerboundHello& client, const HandshakePolicy& policy);

} // namespace arv::protocol
