#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/protocol/auth.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <vector>

TEST(ProtocolAuthTests, AuthChallengeAndResultRoundTrip) {
  arv::protocol::ClientboundAuthChallenge challenge;
  challenge.session_id = arv::SessionID{7};
  challenge.nonce.fill(std::byte{0x11});
  challenge.expires_server_tick = 1234;
  challenge.server_instance_id.fill(std::byte{0x22});

  const std::vector<arv::u8> challenge_bytes =
      arv::protocol::serialize_auth_challenge(challenge);
  const auto decoded_challenge =
      arv::protocol::deserialize_auth_challenge(challenge_bytes);

  ASSERT_TRUE(decoded_challenge);
  EXPECT_EQ(decoded_challenge.value().session_id, arv::SessionID{7});
  EXPECT_EQ(decoded_challenge.value().nonce, challenge.nonce);
  EXPECT_EQ(decoded_challenge.value().expires_server_tick, 1234U);
  EXPECT_EQ(decoded_challenge.value().server_instance_id, challenge.server_instance_id);

  arv::protocol::ClientboundAuthResult result;
  result.accepted = true;
  const arv::crypto::Ed25519KeyPair key_pair = arv::crypto::generate_ed25519_keypair();
  result.profile_id = arv::crypto::derive_profile_id_from_public_key(key_pair.public_key);

  const std::vector<arv::u8> result_bytes =
      arv::protocol::serialize_auth_result(result);
  const auto decoded_result = arv::protocol::deserialize_auth_result(result_bytes);

  ASSERT_TRUE(decoded_result);
  EXPECT_TRUE(decoded_result.value().accepted);
  EXPECT_EQ(decoded_result.value().profile_id, result.profile_id);
}

TEST(ProtocolAuthTests, SignedPayloadVerifiesAndChangesWithSessionContext) {
  const arv::crypto::Ed25519KeyPair key_pair = arv::crypto::generate_ed25519_keypair();
  arv::protocol::AuthSignedPayloadFields fields;
  fields.server_instance_id.fill(std::byte{0x33});
  fields.session_id = arv::SessionID{41};
  fields.profile_id = arv::crypto::derive_profile_id_from_public_key(key_pair.public_key);
  fields.public_key = key_pair.public_key;
  fields.nonce.fill(std::byte{0x44});
  fields.expires_server_tick = 99;

  const std::vector<arv::u8> payload = arv::protocol::build_auth_signed_payload(fields);
  const auto* payload_first = reinterpret_cast<const std::byte*>(payload.data());
  const arv::crypto::Ed25519Signature signature = arv::crypto::sign_ed25519(
      std::span<const std::byte>{payload_first, payload.size()},
      key_pair.private_key);

  EXPECT_TRUE(arv::crypto::verify_ed25519(
      std::span<const std::byte>{payload_first, payload.size()},
      signature,
      key_pair.public_key));

  fields.session_id = arv::SessionID{42};
  const std::vector<arv::u8> changed_payload =
      arv::protocol::build_auth_signed_payload(fields);
  const auto* changed_first = reinterpret_cast<const std::byte*>(changed_payload.data());
  EXPECT_FALSE(arv::crypto::verify_ed25519(
      std::span<const std::byte>{changed_first, changed_payload.size()},
      signature,
      key_pair.public_key));
}
