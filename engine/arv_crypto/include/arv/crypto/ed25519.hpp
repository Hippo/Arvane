#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/crypto/hex.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string_view>

namespace arv::crypto {

inline constexpr usize ed25519_public_key_size = 32;
inline constexpr usize ed25519_private_key_size = 64;
inline constexpr usize ed25519_signature_size = 64;

struct Ed25519PublicKey {
  std::array<std::byte, ed25519_public_key_size> bytes{};

  constexpr bool operator==(const Ed25519PublicKey&) const = default;
};

struct Ed25519PrivateKey {
  std::array<std::byte, ed25519_private_key_size> bytes{};

  constexpr bool operator==(const Ed25519PrivateKey&) const = default;
};

struct Ed25519Signature {
  std::array<std::byte, ed25519_signature_size> bytes{};

  constexpr bool operator==(const Ed25519Signature&) const = default;
};

struct Ed25519KeyPair {
  Ed25519PublicKey public_key{};
  Ed25519PrivateKey private_key{};
};

using Ed25519PublicKeyResult = Expected<Ed25519PublicKey, HexError>;

[[nodiscard]] Ed25519PublicKeyResult parse_prefixed_ed25519_public_key(
    std::string_view text);

[[nodiscard]] Ed25519KeyPair generate_ed25519_keypair() noexcept;

[[nodiscard]] Ed25519Signature sign_ed25519(
    std::span<const std::byte> message,
    const Ed25519PrivateKey& private_key) noexcept;

[[nodiscard]] bool verify_ed25519(
    std::span<const std::byte> message,
    const Ed25519Signature& signature,
    const Ed25519PublicKey& public_key) noexcept;

} // namespace arv::crypto
