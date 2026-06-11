#include "arv/crypto/hash.hpp"

#include "arv/crypto/crypto_init.hpp"
#include "arv/crypto/hex.hpp"

#include "sodium_bytes.hpp"

#include <algorithm>
#include <fstream>
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

Expected<Hash256, HashError> hash_256_file(const std::filesystem::path& path) {
  static_assert(hash_256_size == crypto_generichash_BYTES);
  if (!initialize_crypto()) {
    return Expected<Hash256, HashError>::failure(
        HashError{"crypto initialization failed"});
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Expected<Hash256, HashError>::failure(
        HashError{"failed to open file for hashing: " + path.string()});
  }

  crypto_generichash_state state;
  if (crypto_generichash_init(&state, nullptr, 0, hash_256_size) != 0) {
    return Expected<Hash256, HashError>::failure(
        HashError{"failed to initialize hash state"});
  }

  std::array<unsigned char, 16 * 1024> buffer{};
  while (input) {
    input.read(reinterpret_cast<char*>(buffer.data()), buffer.size());
    const std::streamsize count = input.gcount();
    if (count > 0) {
      if (crypto_generichash_update(
              &state,
              buffer.data(),
              static_cast<unsigned long long>(count)) != 0) {
        return Expected<Hash256, HashError>::failure(
            HashError{"failed to update hash state"});
      }
    }
  }
  if (!input.eof()) {
    return Expected<Hash256, HashError>::failure(
        HashError{"failed to read file for hashing: " + path.string()});
  }

  Hash256 hash;
  if (crypto_generichash_final(
          &state,
          reinterpret_cast<unsigned char*>(hash.bytes.data()),
          hash.bytes.size()) != 0) {
    return Expected<Hash256, HashError>::failure(
        HashError{"failed to finalize hash"});
  }
  return Expected<Hash256, HashError>::success(hash);
}

std::string hash_256_hex(Hash256 hash) {
  return to_lower_hex(hash.bytes);
}

Expected<Hash256, HexError> parse_hash_256_hex(std::string_view text) {
  auto decoded = from_lower_hex(text);
  if (!decoded) {
    return Expected<Hash256, HexError>::failure(decoded.error());
  }
  if (decoded.value().size() != hash_256_size) {
    return Expected<Hash256, HexError>::failure(
        HexError{"hash must be a lowercase 256-bit hex string"});
  }
  Hash256 hash;
  std::copy(decoded.value().begin(), decoded.value().end(), hash.bytes.begin());
  return Expected<Hash256, HexError>::success(hash);
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
