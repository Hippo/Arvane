#pragma once

#include "arv/core/expected.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"

#include <filesystem>
#include <string>

namespace arv::client {

struct LocalIdentityError {
  std::string message{};
};

struct LocalPlayerIdentity {
  crypto::PlayerProfileID profile_id{};
  std::string display_name{"local_player"};
  crypto::Ed25519KeyPair key_pair{};
};

[[nodiscard]] LocalPlayerIdentity make_ephemeral_local_identity(
    std::string display_name = "local_player");

[[nodiscard]] Expected<LocalPlayerIdentity, LocalIdentityError> load_or_create_local_identity(
    const std::filesystem::path& identity_root,
    std::string display_name = "local_player");

} // namespace arv::client
