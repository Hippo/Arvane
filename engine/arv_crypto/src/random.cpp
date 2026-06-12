#include "arv/crypto/random.hpp"

#include "arv/crypto/crypto_init.hpp"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <algorithm>
#include <cstdlib>

#include <sodium.h>

namespace arv::crypto {
namespace {

#ifdef __EMSCRIPTEN__

EM_JS(int, arv_crypto_web_fill_random, (unsigned char* data, int size), {
  const cryptoSource =
      (typeof globalThis !== 'undefined' && globalThis.crypto) ||
      (typeof self !== 'undefined' && self.crypto) ||
      null;
  if (!cryptoSource || typeof cryptoSource.getRandomValues !== 'function') {
    return 0;
  }
  const bytes = new Uint8Array(size);
  cryptoSource.getRandomValues(bytes);
  HEAPU8.set(bytes, data);
  return 1;
});

void fill_random_browser(std::span<std::byte> out) noexcept {
  constexpr usize MaxWebCryptoChunk = 65536U;
  usize offset = 0;
  while (offset < out.size()) {
    const usize chunk = std::min(MaxWebCryptoChunk, out.size() - offset);
    auto* data = reinterpret_cast<unsigned char*>(out.data() + offset);
    if (arv_crypto_web_fill_random(data, static_cast<int>(chunk)) == 0) {
      std::abort();
    }
    offset += chunk;
  }
}

#endif

} // namespace

void fill_random(std::span<std::byte> out) noexcept {
  if (out.empty()) {
    return;
  }
#ifdef __EMSCRIPTEN__
  fill_random_browser(out);
#else
  if (!initialize_crypto()) {
    return;
  }
  randombytes_buf(out.data(), out.size());
#endif
}

} // namespace arv::crypto
