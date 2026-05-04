#include "dedicated_application.hpp"

#include "arv/crypto/crypto_init.hpp"

int main(int argc, char** argv) {
  if (!arv::crypto::initialize_crypto()) {
    return 1;
  }
  return arv::dedicated::run_dedicated_server(argc, argv);
}
