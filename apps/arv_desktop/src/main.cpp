#include "desktop_application.hpp"

#include "arv/crypto/crypto_init.hpp"

int main(int argc, char** argv) {
  if (!arv::crypto::initialize_crypto()) {
    return 1;
  }
  return arv::desktop::run_desktop_client(argc, argv);
}
