#include "arv/protocol/auth.hpp"

#include <utility>

namespace arv::protocol {

std::vector<u8> build_auth_signed_payload(const AuthSignedPayloadFields& fields) {
  MessageWriter writer;
  writer.write_string(AuthSignedPayloadMagic);
  writer.write<u16>(fields.protocol_version);
  writer.write_bytes(fields.server_instance_id);
  writer.write<u64>(fields.session_id.value);
  writer.write_string(fields.profile_id.string());
  writer.write_bytes(fields.public_key.bytes);
  writer.write_bytes(fields.nonce);
  writer.write<u64>(fields.expires_server_tick);
  return std::move(writer).take_bytes();
}

std::vector<u8> serialize_auth_challenge(const ClientboundAuthChallenge& challenge) {
  MessageWriter writer;
  writer.write<u64>(challenge.session_id.value);
  writer.write_bytes(challenge.nonce);
  writer.write<u64>(challenge.expires_server_tick);
  writer.write_bytes(challenge.server_instance_id);
  return std::move(writer).take_bytes();
}

Expected<ClientboundAuthChallenge, ProtocolError> deserialize_auth_challenge(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ClientboundAuthChallenge challenge;

  auto session_id = reader.read<u64>();
  if (!session_id) {
    return Expected<ClientboundAuthChallenge, ProtocolError>::failure(session_id.error());
  }
  challenge.session_id = SessionID{session_id.value()};
  if (auto result = reader.read_bytes(challenge.nonce); !result) {
    return Expected<ClientboundAuthChallenge, ProtocolError>::failure(result.error());
  }
  auto expires = reader.read<u64>();
  if (!expires) {
    return Expected<ClientboundAuthChallenge, ProtocolError>::failure(expires.error());
  }
  challenge.expires_server_tick = expires.value();
  if (auto result = reader.read_bytes(challenge.server_instance_id); !result) {
    return Expected<ClientboundAuthChallenge, ProtocolError>::failure(result.error());
  }
  if (reader.remaining() != 0U) {
    return Expected<ClientboundAuthChallenge, ProtocolError>::failure(
        ProtocolError{"trailing bytes in auth challenge"});
  }
  return Expected<ClientboundAuthChallenge, ProtocolError>::success(challenge);
}

std::vector<u8> serialize_auth_response(const ServerboundAuthResponse& response) {
  MessageWriter writer;
  writer.write_bytes(response.signature.bytes);
  return std::move(writer).take_bytes();
}

Expected<ServerboundAuthResponse, ProtocolError> deserialize_auth_response(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ServerboundAuthResponse response;
  if (auto result = reader.read_bytes(response.signature.bytes); !result) {
    return Expected<ServerboundAuthResponse, ProtocolError>::failure(result.error());
  }
  if (reader.remaining() != 0U) {
    return Expected<ServerboundAuthResponse, ProtocolError>::failure(
        ProtocolError{"trailing bytes in auth response"});
  }
  return Expected<ServerboundAuthResponse, ProtocolError>::success(response);
}

std::vector<u8> serialize_auth_result(const ClientboundAuthResult& result) {
  MessageWriter writer;
  writer.write_bool(result.accepted);
  writer.write_string(result.profile_id.string());
  writer.write_string(result.reason_if_rejected);
  return std::move(writer).take_bytes();
}

Expected<ClientboundAuthResult, ProtocolError> deserialize_auth_result(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  ClientboundAuthResult result;
  auto accepted = reader.read_bool();
  auto profile_id = reader.read_string();
  auto reason = reader.read_string();
  if (!accepted || !profile_id || !reason) {
    return Expected<ClientboundAuthResult, ProtocolError>::failure(
        ProtocolError{"failed to read auth result"});
  }
  result.accepted = accepted.value();
  auto parsed_profile_id = crypto::parse_player_profile_id(profile_id.value());
  if (!parsed_profile_id) {
    return Expected<ClientboundAuthResult, ProtocolError>::failure(
        ProtocolError{parsed_profile_id.error().message});
  }
  result.profile_id = parsed_profile_id.value();
  result.reason_if_rejected = std::move(reason.value());
  if (reader.remaining() != 0U) {
    return Expected<ClientboundAuthResult, ProtocolError>::failure(
        ProtocolError{"trailing bytes in auth result"});
  }
  return Expected<ClientboundAuthResult, ProtocolError>::success(std::move(result));
}

} // namespace arv::protocol
