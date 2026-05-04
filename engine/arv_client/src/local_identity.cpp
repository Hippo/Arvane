#include "arv/client/local_identity.hpp"

#include "arv/crypto/hash.hpp"
#include "arv/crypto/hex.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string_view>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace arv::client {
namespace {

using json = nlohmann::json;

inline constexpr u32 LocalIdentityVersion = 1;
inline constexpr std::string_view LocalIdentityFileName = "local_identity.json";
inline constexpr std::string_view LocalPrivateKeyFileName = "local_private_key.bin";

[[nodiscard]] std::filesystem::path identity_json_path(const std::filesystem::path& root) {
  return root / std::string{LocalIdentityFileName};
}

[[nodiscard]] std::filesystem::path private_key_path(const std::filesystem::path& root) {
  return root / std::string{LocalPrivateKeyFileName};
}

[[nodiscard]] Expected<std::string, LocalIdentityError> read_text_file(
    const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    return Expected<std::string, LocalIdentityError>::failure(
        LocalIdentityError{"failed to open local identity file: " + path.string()});
  }
  return Expected<std::string, LocalIdentityError>::success(
      std::string{std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}});
}

[[nodiscard]] Expected<crypto::Ed25519PrivateKey, LocalIdentityError> read_private_key(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Expected<crypto::Ed25519PrivateKey, LocalIdentityError>::failure(
        LocalIdentityError{"failed to open local private key file: " + path.string()});
  }

  crypto::Ed25519PrivateKey private_key;
  input.read(
      reinterpret_cast<char*>(private_key.bytes.data()),
      static_cast<std::streamsize>(private_key.bytes.size()));
  if (!input || input.peek() != std::char_traits<char>::eof()) {
    return Expected<crypto::Ed25519PrivateKey, LocalIdentityError>::failure(
        LocalIdentityError{"local private key file has the wrong size"});
  }
  return Expected<crypto::Ed25519PrivateKey, LocalIdentityError>::success(private_key);
}

[[nodiscard]] Expected<crypto::Ed25519PublicKey, LocalIdentityError> public_key_from_hex(
    std::string_view text) {
  auto bytes = crypto::from_prefixed_lower_hex(text);
  if (!bytes) {
    return Expected<crypto::Ed25519PublicKey, LocalIdentityError>::failure(
        LocalIdentityError{bytes.error().message});
  }
  if (bytes.value().size() != crypto::ed25519_public_key_size) {
    return Expected<crypto::Ed25519PublicKey, LocalIdentityError>::failure(
        LocalIdentityError{"local identity public key has the wrong size"});
  }

  crypto::Ed25519PublicKey public_key;
  std::copy(bytes.value().begin(), bytes.value().end(), public_key.bytes.begin());
  return Expected<crypto::Ed25519PublicKey, LocalIdentityError>::success(public_key);
}

[[nodiscard]] Expected<LocalPlayerIdentity, LocalIdentityError> read_local_identity(
    const std::filesystem::path& root) {
  auto text = read_text_file(identity_json_path(root));
  if (!text) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(text.error());
  }

  try {
    const json node = json::parse(text.value());
    if (node.at("version").get<u32>() != LocalIdentityVersion) {
      return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
          LocalIdentityError{"unsupported local identity version"});
    }

    auto public_key = public_key_from_hex(node.at("public_key").get<std::string>());
    auto private_key = read_private_key(root / node.at("private_key_file").get<std::string>());
    if (!public_key) {
      return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(public_key.error());
    }
    if (!private_key) {
      return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(private_key.error());
    }

    LocalPlayerIdentity identity;
    identity.key_pair.public_key = public_key.value();
    identity.key_pair.private_key = private_key.value();
    identity.profile_id = crypto::derive_profile_id_from_public_key(identity.key_pair.public_key);
    identity.display_name = node.value("display_name", std::string{"local_player"});
    auto saved_profile_id =
        crypto::parse_player_profile_id(node.at("profile_id").get<std::string>());
    if (!saved_profile_id) {
      return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
          LocalIdentityError{saved_profile_id.error().message});
    }
    if (saved_profile_id.value() != identity.profile_id) {
      return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
          LocalIdentityError{"local identity profile id does not match public key"});
    }
    return Expected<LocalPlayerIdentity, LocalIdentityError>::success(std::move(identity));
  } catch (const json::exception& ex) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(
        LocalIdentityError{"failed to parse local identity JSON: " + std::string{ex.what()}});
  }
}

[[nodiscard]] Expected<void, LocalIdentityError> write_identity_files(
    const std::filesystem::path& root,
    const LocalPlayerIdentity& identity) {
  std::error_code error;
  std::filesystem::create_directories(root, error);
  if (error) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to create identity directory: " + error.message()});
  }

  {
    std::ofstream private_key(private_key_path(root), std::ios::binary | std::ios::trunc);
    if (!private_key) {
      return Expected<void, LocalIdentityError>::failure(
          LocalIdentityError{"failed to write local private key file"});
    }
    private_key.write(
        reinterpret_cast<const char*>(identity.key_pair.private_key.bytes.data()),
        static_cast<std::streamsize>(identity.key_pair.private_key.bytes.size()));
    if (!private_key) {
      return Expected<void, LocalIdentityError>::failure(
          LocalIdentityError{"failed to write local private key bytes"});
    }
  }

  const json node{
      {"version", LocalIdentityVersion},
      {"profile_id", identity.profile_id.string()},
      {"display_name", identity.display_name},
      {"public_key", crypto::to_prefixed_lower_hex(identity.key_pair.public_key.bytes)},
      {"private_key_file", std::string{LocalPrivateKeyFileName}},
  };
  std::ofstream metadata(identity_json_path(root), std::ios::trunc);
  if (!metadata) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to write local identity metadata"});
  }
  metadata << node.dump(2);
  if (!metadata) {
    return Expected<void, LocalIdentityError>::failure(
        LocalIdentityError{"failed to write local identity metadata bytes"});
  }
  return Expected<void, LocalIdentityError>::success();
}

} // namespace

LocalPlayerIdentity make_ephemeral_local_identity(std::string display_name) {
  LocalPlayerIdentity identity;
  identity.display_name = display_name.empty() ? "local_player" : std::move(display_name);
  identity.key_pair = crypto::generate_ed25519_keypair();
  identity.profile_id = crypto::derive_profile_id_from_public_key(identity.key_pair.public_key);
  return identity;
}

Expected<LocalPlayerIdentity, LocalIdentityError> load_or_create_local_identity(
    const std::filesystem::path& identity_root,
    std::string display_name) {
  std::error_code error;
  const bool has_metadata = std::filesystem::exists(identity_json_path(identity_root), error);
  error.clear();
  const bool has_private_key = std::filesystem::exists(private_key_path(identity_root), error);
  if (has_metadata && has_private_key) {
    return read_local_identity(identity_root);
  }

  LocalPlayerIdentity identity = make_ephemeral_local_identity(std::move(display_name));
  auto write_result = write_identity_files(identity_root, identity);
  if (!write_result) {
    return Expected<LocalPlayerIdentity, LocalIdentityError>::failure(write_result.error());
  }
  return Expected<LocalPlayerIdentity, LocalIdentityError>::success(std::move(identity));
}

} // namespace arv::client
