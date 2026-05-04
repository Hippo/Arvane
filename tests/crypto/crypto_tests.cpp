#include "arv/crypto/crypto_init.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/crypto/hex.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::vector<std::byte> bytes_for(std::string_view text) {
  const auto* first = reinterpret_cast<const std::byte*>(text.data());
  return {first, first + text.size()};
}

} // namespace

TEST(CryptoTests, InitializesSodiumWrapper) {
  EXPECT_TRUE(arv::crypto::initialize_crypto());
}

TEST(CryptoTests, Ed25519SignVerifyAcceptsMatchingKey) {
  const arv::crypto::Ed25519KeyPair key_pair = arv::crypto::generate_ed25519_keypair();
  const std::vector<std::byte> message = bytes_for("arvane auth payload");

  const arv::crypto::Ed25519Signature signature =
      arv::crypto::sign_ed25519(message, key_pair.private_key);

  EXPECT_TRUE(arv::crypto::verify_ed25519(message, signature, key_pair.public_key));
}

TEST(CryptoTests, Ed25519VerifyRejectsWrongKeyAndModifiedPayload) {
  const arv::crypto::Ed25519KeyPair key_pair = arv::crypto::generate_ed25519_keypair();
  const arv::crypto::Ed25519KeyPair wrong_key_pair = arv::crypto::generate_ed25519_keypair();
  std::vector<std::byte> message = bytes_for("arvane auth payload");
  const arv::crypto::Ed25519Signature signature =
      arv::crypto::sign_ed25519(message, key_pair.private_key);

  EXPECT_FALSE(arv::crypto::verify_ed25519(message, signature, wrong_key_pair.public_key));

  message[0] = std::byte{'A'};
  EXPECT_FALSE(arv::crypto::verify_ed25519(message, signature, key_pair.public_key));
}

TEST(CryptoTests, Hash256IsStable) {
  const std::vector<std::byte> message = bytes_for("stable hash input");

  const arv::crypto::Hash256 lhs = arv::crypto::hash_256(message);
  const arv::crypto::Hash256 rhs = arv::crypto::hash_256(message);

  EXPECT_EQ(lhs, rhs);
}

TEST(CryptoTests, HexEncodingIsLowercasePrefixedAndRejectsMalformedText) {
  const std::vector<std::byte> data{std::byte{0xAB}, std::byte{0x0C}, std::byte{0x42}};

  EXPECT_EQ(arv::crypto::to_lower_hex(data), "ab0c42");
  EXPECT_EQ(arv::crypto::to_prefixed_lower_hex(data), "0xab0c42");
  EXPECT_TRUE(arv::crypto::from_prefixed_lower_hex("0xab0c42"));
  EXPECT_FALSE(arv::crypto::from_prefixed_lower_hex("ab0c42"));
  EXPECT_FALSE(arv::crypto::from_prefixed_lower_hex("0xAB0C42"));
}

TEST(CryptoTests, ProfileIdDerivesFromPublicKeyAsPrefixedHash) {
  const arv::crypto::Ed25519KeyPair key_pair = arv::crypto::generate_ed25519_keypair();

  const arv::crypto::PlayerProfileID profile_id =
      arv::crypto::derive_profile_id_from_public_key(key_pair.public_key);

  EXPECT_EQ(profile_id.string().rfind("0x", 0), 0U);
  EXPECT_EQ(profile_id.string().size(), 66U);
  EXPECT_TRUE(arv::crypto::is_player_profile_id(profile_id.string()));
}
