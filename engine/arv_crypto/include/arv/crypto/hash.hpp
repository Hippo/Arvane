#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hex.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace arv::crypto {

inline constexpr usize hash_256_size = 32;

struct Hash256 {
  std::array<std::byte, hash_256_size> bytes{};

  constexpr bool operator==(const Hash256&) const = default;
};

struct HashError {
  std::string message{};
};

[[nodiscard]] Hash256 hash_256(std::span<const std::byte> data) noexcept;
[[nodiscard]] Expected<Hash256, HashError> hash_256_file(
    const std::filesystem::path& path);
[[nodiscard]] std::string hash_256_hex(Hash256 hash);
[[nodiscard]] Expected<Hash256, HexError> parse_hash_256_hex(std::string_view text);

class PlayerProfileID {
public:
  PlayerProfileID() = default;

  [[nodiscard]] static PlayerProfileID from_hash(Hash256 hash);

  [[nodiscard]] const std::string& string() const noexcept {
    return value_;
  }

  [[nodiscard]] bool empty() const noexcept {
    return value_.empty();
  }

  friend bool operator==(const PlayerProfileID&, const PlayerProfileID&) = default;
  friend bool operator!=(const PlayerProfileID&, const PlayerProfileID&) = default;
  friend bool operator<(const PlayerProfileID& lhs, const PlayerProfileID& rhs) noexcept {
    return lhs.value_ < rhs.value_;
  }
  friend Expected<PlayerProfileID, HexError> parse_player_profile_id(std::string_view text);

private:
  explicit PlayerProfileID(std::string value);

  std::string value_{};
};

[[nodiscard]] bool is_player_profile_id(std::string_view text) noexcept;
[[nodiscard]] Expected<PlayerProfileID, HexError> parse_player_profile_id(
    std::string_view text);

[[nodiscard]] PlayerProfileID derive_profile_id_from_public_key(
    const Ed25519PublicKey& public_key);

} // namespace arv::crypto
