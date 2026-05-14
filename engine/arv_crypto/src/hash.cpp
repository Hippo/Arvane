#include "arv/crypto/hash.hpp"

#include "arv/crypto/crypto_init.hpp"
#include "arv/crypto/hex.hpp"

#include "sodium_bytes.hpp"

#include <utility>

#include <sodium.h>

namespace arv::crypto {

Hash256 hash_256(std::span<const std::byte> data) noexcept {
  static_assert(hash_256_size == crypto_generichash_BYTES);
  Hash256 hash;
  if (!initialize_crypto()) {
    return hash;
  }
  if (crypto_generichash(
          reinterpret_cast<unsigned char*>(hash.bytes.data()),
          hash.bytes.size(),
          detail::bytes_data(data),
          static_cast<unsigned long long>(data.size()),
          nullptr,
          0) != 0) {
    hash.bytes.fill(std::byte{0});
  }
  return hash;
}

PlayerProfileID::PlayerProfileID(std::string value) : value_(std::move(value)) {
}

PlayerProfileID PlayerProfileID::from_hash(Hash256 hash) {
  return PlayerProfileID{to_prefixed_lower_hex(hash.bytes)};
}

bool is_player_profile_id(std::string_view text) noexcept {
  return text.size() == 66U && is_prefixed_lower_hex(text);
}

Expected<PlayerProfileID, HexError> parse_player_profile_id(std::string_view text) {
  if (text.empty()) {
    return Expected<PlayerProfileID, HexError>::success(PlayerProfileID{});
  }
  if (!is_player_profile_id(text)) {
    return Expected<PlayerProfileID, HexError>::failure(
        HexError{"profile id must be a 0x-prefixed lowercase 256-bit hash"});
  }
  return Expected<PlayerProfileID, HexError>::success(PlayerProfileID{std::string{text}});
}

PlayerProfileID derive_profile_id_from_public_key(const Ed25519PublicKey& public_key) {
  const Hash256 hash = hash_256(public_key.bytes);
  return PlayerProfileID::from_hash(hash);
}

} // namespace arv::crypto
