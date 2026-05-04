#include "arv/crypto/crypto_init.hpp"

#include <sodium.h>

namespace arv::crypto {

bool initialize_crypto() noexcept {
  return sodium_init() >= 0;
}

} // namespace arv::crypto
