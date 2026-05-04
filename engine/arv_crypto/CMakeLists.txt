set(ARV_CRYPTO_HEADERS
  include/arv/crypto/crypto_init.hpp
  include/arv/crypto/ed25519.hpp
  include/arv/crypto/hash.hpp
  include/arv/crypto/hex.hpp
  include/arv/crypto/random.hpp
)

add_library(arv_crypto STATIC
  src/crypto_init.cpp
  src/ed25519.cpp
  src/hash.cpp
  src/hex.cpp
  src/random.cpp
)

target_include_directories(arv_crypto PUBLIC include)
target_link_libraries(arv_crypto PUBLIC arv_core)

if(TARGET sodium)
  set(ARV_CRYPTO_SODIUM_TARGET sodium)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SODIUM REQUIRED IMPORTED_TARGET libsodium)
  set(ARV_CRYPTO_SODIUM_TARGET PkgConfig::SODIUM)
endif()

target_link_libraries(arv_crypto PRIVATE ${ARV_CRYPTO_SODIUM_TARGET})
