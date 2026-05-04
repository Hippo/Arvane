#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/protocol/message_writer.hpp"
#include "arv/protocol/protocol_version.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::protocol {

inline constexpr usize AuthChallengeNonceSize = 32;
inline constexpr usize AuthServerInstanceIDSize = 16;
inline constexpr std::string_view AuthSignedPayloadMagic = "ARVANE_AUTH_V1";

using AuthChallengeNonce = std::array<std::byte, AuthChallengeNonceSize>;
using AuthServerInstanceID = std::array<std::byte, AuthServerInstanceIDSize>;

struct ServerboundAuthBegin {
  crypto::Ed25519PublicKey public_key{};
  std::string display_name{"local_player"};
  crypto::PlayerProfileID claimed_profile_id{};
};

struct ClientboundAuthChallenge {
  SessionID session_id{};
  AuthChallengeNonce nonce{};
  u64 expires_server_tick{};
  AuthServerInstanceID server_instance_id{};
};

struct ServerboundAuthResponse {
  crypto::Ed25519Signature signature{};
};

struct ClientboundAuthResult {
  bool accepted{};
  crypto::PlayerProfileID profile_id{};
  std::string reason_if_rejected{};
};

struct AuthSignedPayloadFields {
  u16 protocol_version{CurrentProtocolVersion};
  AuthServerInstanceID server_instance_id{};
  SessionID session_id{};
  crypto::PlayerProfileID profile_id{};
  crypto::Ed25519PublicKey public_key{};
  AuthChallengeNonce nonce{};
  u64 expires_server_tick{};
};

[[nodiscard]] std::vector<u8> build_auth_signed_payload(
    const AuthSignedPayloadFields& fields);

[[nodiscard]] std::vector<u8> serialize_auth_challenge(
    const ClientboundAuthChallenge& challenge);
[[nodiscard]] Expected<ClientboundAuthChallenge, ProtocolError> deserialize_auth_challenge(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_auth_response(
    const ServerboundAuthResponse& response);
[[nodiscard]] Expected<ServerboundAuthResponse, ProtocolError> deserialize_auth_response(
    std::span<const u8> bytes);

[[nodiscard]] std::vector<u8> serialize_auth_result(
    const ClientboundAuthResult& result);
[[nodiscard]] Expected<ClientboundAuthResult, ProtocolError> deserialize_auth_result(
    std::span<const u8> bytes);

} // namespace arv::protocol
