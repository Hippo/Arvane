#include "arv/crypto/ed25519.hpp"

#include "arv/crypto/crypto_init.hpp"

#include <sodium.h>

namespace arv::crypto {
namespace {

[[nodiscard]] const unsigned char* bytes_data(std::span<const std::byte> bytes) noexcept {
  if (bytes.empty()) {
    return nullptr;
  }
  return reinterpret_cast<const unsigned char*>(bytes.data());
}

} // namespace

Ed25519KeyPair generate_ed25519_keypair() noexcept {
  static_assert(ed25519_public_key_size == crypto_sign_PUBLICKEYBYTES);
  static_assert(ed25519_private_key_size == crypto_sign_SECRETKEYBYTES);
  Ed25519KeyPair key_pair;
  if (!initialize_crypto()) {
    return key_pair;
  }
  (void)crypto_sign_keypair(
      reinterpret_cast<unsigned char*>(key_pair.public_key.bytes.data()),
      reinterpret_cast<unsigned char*>(key_pair.private_key.bytes.data()));
  return key_pair;
}

Ed25519Signature sign_ed25519(
    std::span<const std::byte> message,
    const Ed25519PrivateKey& private_key) noexcept {
  static_assert(ed25519_signature_size == crypto_sign_BYTES);
  Ed25519Signature signature;
  if (!initialize_crypto()) {
    return signature;
  }
  if (crypto_sign_detached(
          reinterpret_cast<unsigned char*>(signature.bytes.data()),
          nullptr,
          bytes_data(message),
          static_cast<unsigned long long>(message.size()),
          reinterpret_cast<const unsigned char*>(private_key.bytes.data())) != 0) {
    signature.bytes.fill(std::byte{0});
  }
  return signature;
}

bool verify_ed25519(
    std::span<const std::byte> message,
    const Ed25519Signature& signature,
    const Ed25519PublicKey& public_key) noexcept {
  if (!initialize_crypto()) {
    return false;
  }
  return crypto_sign_verify_detached(
             reinterpret_cast<const unsigned char*>(signature.bytes.data()),
             bytes_data(message),
             static_cast<unsigned long long>(message.size()),
             reinterpret_cast<const unsigned char*>(public_key.bytes.data())) == 0;
}

} // namespace arv::crypto
