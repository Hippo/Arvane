#include "arv/crypto/random.hpp"

#include "arv/crypto/crypto_init.hpp"

#include <sodium.h>

namespace arv::crypto {

void fill_random(std::span<std::byte> out) noexcept {
  if (out.empty() || !initialize_crypto()) {
    return;
  }
  randombytes_buf(out.data(), out.size());
}

} // namespace arv::crypto
