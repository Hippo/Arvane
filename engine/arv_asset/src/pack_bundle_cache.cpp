#include "arv/asset/pack_bundle_cache.hpp"

#include "arv/crypto/hash.hpp"

#include <cstddef>
#include <fstream>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>
#include <utility>

#include <zstd.h>

namespace arv::asset {
namespace {

inline constexpr u32 MaxBundlePathBytes = 64U * 1024U;
inline constexpr usize BundleHashHexSize = crypto::hash_256_size * 2U;
inline constexpr std::string_view CacheCompletionMarkerName = ".arvane-pack-cache";
inline constexpr std::string_view CacheMarkerHashPrefix = "bundle_hash=";

struct BundleFileEntry {
  std::string path{};
  std::vector<u8> bytes{};
  std::string hash{};
};

struct DecodedBundle {
  std::string bundle_hash{};
  std::string manifest_json{};
  std::vector<BundleFileEntry> files{};
};

[[nodiscard]] PackBundleError bundle_error(std::string message) {
  return PackBundleError{std::move(message)};
}

[[nodiscard]] std::span<const std::byte> bytes_view(std::span<const u8> bytes) noexcept {
  return {
      reinterpret_cast<const std::byte*>(bytes.data()),
      bytes.size()};
}

[[nodiscard]] std::string hash_bytes_hex(std::span<const u8> bytes) {
  return crypto::hash_256_hex(crypto::hash_256(bytes_view(bytes)));
}

[[nodiscard]] std::optional<u32> read_u32(std::span<const u8> bytes, usize& offset) {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(u32)) {
    return std::nullopt;
  }
  u32 value = 0;
  for (u32 shift = 0; shift < 32U; shift += 8U) {
    value |= static_cast<u32>(bytes[offset++]) << shift;
  }
  return value;
}

[[nodiscard]] std::optional<u64> read_u64(std::span<const u8> bytes, usize& offset) {
  if (offset > bytes.size() || bytes.size() - offset < sizeof(u64)) {
    return std::nullopt;
  }
  u64 value = 0;
  for (u32 shift = 0; shift < 64U; shift += 8U) {
    value |= static_cast<u64>(bytes[offset++]) << shift;
  }
  return value;
}

[[nodiscard]] bool has_prefix(std::string_view text, std::string_view prefix) noexcept {
  return text.size() >= prefix.size() && text.substr(0U, prefix.size()) == prefix;
}

[[nodiscard]] bool executable_extension(std::string_view path) noexcept {
  const usize dot = path.find_last_of('.');
  if (dot == std::string_view::npos) {
    return false;
  }
  const std::string_view ext = path.substr(dot);
  return ext == ".dll" || ext == ".so" || ext == ".dylib" || ext == ".exe";
}

[[nodiscard]] bool safe_generic_path(std::string_view path) noexcept {
  if (path.empty() || path.front() == '/' ||
      path.find('\\') != std::string_view::npos ||
      path.find(':') != std::string_view::npos) {
    return false;
  }
  for (const char ch : path) {
    const auto value = static_cast<unsigned char>(ch);
    if (value < 0x20U || value == 0x7FU) {
      return false;
    }
  }
  usize start = 0U;
  while (start <= path.size()) {
    const usize slash = path.find('/', start);
    const std::string_view segment =
        slash == std::string_view::npos ? path.substr(start) : path.substr(start, slash - start);
    if (segment.empty() || segment == "." || segment == "..") {
      return false;
    }
    if (slash == std::string_view::npos) {
      break;
    }
    start = slash + 1U;
  }
  return true;
}

[[nodiscard]] bool allowed_assets_path(std::string_view path) noexcept {
  if (!has_prefix(path, "assets/")) {
    return false;
  }
  const std::string_view rest = path.substr(std::string_view{"assets/"}.size());
  const usize slash = rest.find('/');
  if (slash == std::string_view::npos || slash == 0U || slash + 1U >= rest.size()) {
    return false;
  }
  const std::string_view category = rest.substr(slash + 1U);
  return has_prefix(category, "textures/") ||
         has_prefix(category, "models/") ||
         has_prefix(category, "animations/") ||
         has_prefix(category, "fonts/") ||
         has_prefix(category, "lang/") ||
         has_prefix(category, "sounds/");
}

[[nodiscard]] bool allowed_data_path(std::string_view path) noexcept {
  if (!has_prefix(path, "data/")) {
    return false;
  }
  const std::string_view rest = path.substr(std::string_view{"data/"}.size());
  const usize slash = rest.find('/');
  if (slash == std::string_view::npos || slash == 0U || slash + 1U >= rest.size()) {
    return false;
  }
  const std::string_view leaf = rest.substr(slash + 1U);
  return leaf == "blocks.json" ||
         leaf == "container_layouts.json" ||
         leaf == "container_types.json" ||
         leaf == "items.json" ||
         leaf == "entities.json" ||
         leaf == "vehicles.json" ||
         leaf == "item_animations.json" ||
         has_prefix(leaf, "crafting_categories/") ||
         has_prefix(leaf, "recipes/") ||
         has_prefix(leaf, "lang/") ||
         has_prefix(leaf, "particles/");
}

[[nodiscard]] Expected<std::vector<u8>, PackBundleError> read_binary_file(
    const std::filesystem::path& path,
    u64 max_size) {
  std::error_code error;
  const u64 size = std::filesystem::file_size(path, error);
  if (error) {
    return Expected<std::vector<u8>, PackBundleError>::failure(
        bundle_error("failed to inspect file: " + path.string()));
  }
  if (size > max_size) {
    return Expected<std::vector<u8>, PackBundleError>::failure(
        bundle_error("client bundle file exceeds size limit: " + path.string()));
  }

  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return Expected<std::vector<u8>, PackBundleError>::failure(
        bundle_error("failed to open file: " + path.string()));
  }
  std::vector<u8> bytes(static_cast<usize>(size));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
  }
  if (!input && !input.eof()) {
    return Expected<std::vector<u8>, PackBundleError>::failure(
        bundle_error("failed to read file: " + path.string()));
  }
  return Expected<std::vector<u8>, PackBundleError>::success(std::move(bytes));
}

[[nodiscard]] Expected<std::vector<u8>, PackBundleError> decompress_payload(
    std::span<const u8> compressed,
    u64 expected_uncompressed_size,
    const ClientPackBundleLimits& limits) {
  if (compressed.size() > limits.max_compressed_size) {
    return Expected<std::vector<u8>, PackBundleError>::failure(
        bundle_error("compressed bundle exceeds size limit"));
  }
  if (expected_uncompressed_size > limits.max_uncompressed_size) {
    return Expected<std::vector<u8>, PackBundleError>::failure(
        bundle_error("uncompressed bundle exceeds size limit"));
  }
  std::vector<u8> payload(static_cast<usize>(expected_uncompressed_size));
  const usize decompressed_size = ZSTD_decompress(
      payload.data(),
      payload.size(),
      compressed.data(),
      compressed.size());
  if (ZSTD_isError(decompressed_size)) {
    return Expected<std::vector<u8>, PackBundleError>::failure(
        bundle_error("zstd decompression failed: " + std::string{ZSTD_getErrorName(decompressed_size)}));
  }
  if (decompressed_size != expected_uncompressed_size) {
    return Expected<std::vector<u8>, PackBundleError>::failure(
        bundle_error("zstd decompressed size mismatch"));
  }
  return Expected<std::vector<u8>, PackBundleError>::success(std::move(payload));
}

[[nodiscard]] Expected<DecodedBundle, PackBundleError> decode_apack(
    std::span<const u8> apack,
    std::string_view expected_bundle_hash,
    const ClientPackBundleLimits& limits) {
  usize offset = 0U;
  if (apack.size() < ClientPackBundleMagic.size() ||
      std::string_view{reinterpret_cast<const char*>(apack.data()), ClientPackBundleMagic.size()} !=
          ClientPackBundleMagic) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("invalid client pack bundle magic"));
  }
  offset += ClientPackBundleMagic.size();

  const std::optional<u32> version = read_u32(apack, offset);
  const std::optional<u32> manifest_size = read_u32(apack, offset);
  if (!version.has_value() || !manifest_size.has_value()) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("truncated client pack bundle header"));
  }
  if (version.value() != ClientPackBundleFormatVersion) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("unsupported client pack bundle version"));
  }
  if (apack.size() - offset < manifest_size.value()) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("truncated client pack bundle manifest"));
  }
  std::string manifest_json{
      reinterpret_cast<const char*>(apack.data() + offset),
      manifest_size.value()};
  offset += manifest_size.value();

  const std::optional<u64> compressed_size = read_u64(apack, offset);
  const std::optional<u64> uncompressed_size = read_u64(apack, offset);
  if (!compressed_size.has_value() || !uncompressed_size.has_value()) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("truncated client pack bundle sizes"));
  }
  if (apack.size() - offset < BundleHashHexSize) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("truncated client pack bundle hash"));
  }
  std::string bundle_hash{
      reinterpret_cast<const char*>(apack.data() + offset),
      BundleHashHexSize};
  offset += BundleHashHexSize;
  if (!is_valid_bundle_hash(bundle_hash)) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("invalid client pack bundle hash"));
  }
  if (!expected_bundle_hash.empty() && expected_bundle_hash != bundle_hash) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("client pack bundle hash does not match manifest"));
  }
  if (compressed_size.value() > limits.max_compressed_size ||
      apack.size() - offset != compressed_size.value()) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("client pack bundle compressed size mismatch"));
  }

  const std::span<const u8> compressed{apack.data() + offset, static_cast<usize>(compressed_size.value())};
  auto payload = decompress_payload(compressed, uncompressed_size.value(), limits);
  if (!payload) {
    return Expected<DecodedBundle, PackBundleError>::failure(payload.error());
  }
  if (hash_bytes_hex(payload.value()) != bundle_hash) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("client pack bundle payload hash mismatch"));
  }

  std::span<const u8> payload_bytes{payload.value().data(), payload.value().size()};
  usize payload_offset = 0U;
  const std::optional<u32> file_count = read_u32(payload_bytes, payload_offset);
  if (!file_count.has_value()) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("truncated client pack bundle payload"));
  }
  if (file_count.value() > limits.max_files) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("client pack bundle has too many files"));
  }

  DecodedBundle decoded;
  decoded.bundle_hash = std::move(bundle_hash);
  decoded.manifest_json = std::move(manifest_json);
  HashSet<std::string> seen_paths;
  decoded.files.reserve(file_count.value());
  for (u32 index = 0; index < file_count.value(); ++index) {
    const std::optional<u32> path_size = read_u32(payload_bytes, payload_offset);
    if (!path_size.has_value() || path_size.value() == 0U ||
        path_size.value() > MaxBundlePathBytes ||
        payload_bytes.size() - payload_offset < path_size.value()) {
      return Expected<DecodedBundle, PackBundleError>::failure(
          bundle_error("invalid client pack bundle path"));
    }
    std::string path{
        reinterpret_cast<const char*>(payload_bytes.data() + payload_offset),
        path_size.value()};
    payload_offset += path_size.value();
    if (!is_client_bundle_relative_path_allowed(path)) {
      return Expected<DecodedBundle, PackBundleError>::failure(
          bundle_error("unsafe client pack bundle path: " + path));
    }
    if (!seen_paths.insert(path).second) {
      return Expected<DecodedBundle, PackBundleError>::failure(
          bundle_error("duplicate client pack bundle path: " + path));
    }

    const std::optional<u64> file_size = read_u64(payload_bytes, payload_offset);
    if (!file_size.has_value() || file_size.value() > limits.max_file_size ||
        payload_bytes.size() - payload_offset < BundleHashHexSize + file_size.value()) {
      return Expected<DecodedBundle, PackBundleError>::failure(
          bundle_error("invalid client pack bundle file size"));
    }
    std::string file_hash{
        reinterpret_cast<const char*>(payload_bytes.data() + payload_offset),
        BundleHashHexSize};
    payload_offset += BundleHashHexSize;
    if (!is_valid_bundle_hash(file_hash)) {
      return Expected<DecodedBundle, PackBundleError>::failure(
          bundle_error("invalid client pack bundle file hash"));
    }
    std::vector<u8> bytes(
        payload_bytes.begin() + static_cast<std::ptrdiff_t>(payload_offset),
        payload_bytes.begin() + static_cast<std::ptrdiff_t>(payload_offset + file_size.value()));
    payload_offset += static_cast<usize>(file_size.value());
    if (hash_bytes_hex(bytes) != file_hash) {
      return Expected<DecodedBundle, PackBundleError>::failure(
          bundle_error("client pack bundle file hash mismatch: " + path));
    }
    decoded.files.push_back({std::move(path), std::move(bytes), std::move(file_hash)});
  }
  if (payload_offset != payload_bytes.size()) {
    return Expected<DecodedBundle, PackBundleError>::failure(
        bundle_error("trailing bytes in client pack bundle payload"));
  }
  return Expected<DecodedBundle, PackBundleError>::success(std::move(decoded));
}

[[nodiscard]] PackBundleResult write_file_bytes(
    const std::filesystem::path& path,
    std::span<const u8> bytes) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    return PackBundleResult::failure(
        bundle_error("failed to create cache directory: " + error.message()));
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return PackBundleResult::failure(
        bundle_error("failed to open cache file for write: " + path.string()));
  }
  if (!bytes.empty()) {
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  output.flush();
  if (!output) {
    return PackBundleResult::failure(
        bundle_error("failed to write cache file: " + path.string()));
  }
  return PackBundleResult::success();
}

[[nodiscard]] bool cache_marker_matches(
    const std::filesystem::path& cache_path,
    std::string_view bundle_hash) {
  std::ifstream input(cache_path / std::filesystem::path{std::string{CacheCompletionMarkerName}},
                      std::ios::binary);
  if (!input) {
    return false;
  }
  std::string line;
  std::getline(input, line);
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  if (line != std::string{CacheMarkerHashPrefix} + std::string{bundle_hash}) {
    return false;
  }

  bool saw_manifest = false;
  bool saw_registry = false;
  HashSet<std::string> seen_paths;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty() || line == "format_version=1") {
      continue;
    }
    constexpr std::string_view file_prefix = "file\t";
    if (!has_prefix(line, file_prefix)) {
      return false;
    }
    const std::string_view rest = std::string_view{line}.substr(file_prefix.size());
    const usize separator = rest.find('\t');
    if (separator == std::string_view::npos) {
      return false;
    }
    const std::string_view file_hash = rest.substr(0U, separator);
    const std::string_view relative_path = rest.substr(separator + 1U);
    if (!is_valid_bundle_hash(file_hash) ||
        !is_client_bundle_relative_path_allowed(relative_path)) {
      return false;
    }
    std::string path{relative_path};
    if (!seen_paths.insert(path).second) {
      return false;
    }
    auto bytes = read_binary_file(
        cache_path / std::filesystem::path{path},
        ClientPackBundleLimits{}.max_file_size);
    if (!bytes || hash_bytes_hex(bytes.value()) != file_hash) {
      return false;
    }
    saw_manifest = saw_manifest || path == "manifest.json";
    saw_registry = saw_registry || path == "registry-client.json";
  }
  return saw_manifest && saw_registry;
}

[[nodiscard]] PackBundleResult write_cache_marker(
    const std::filesystem::path& cache_path,
    std::string_view bundle_hash,
    std::span<const BundleFileEntry> files) {
  std::string marker =
      std::string{CacheMarkerHashPrefix} + std::string{bundle_hash} + "\nformat_version=1\n";
  for (const BundleFileEntry& file : files) {
    marker += "file\t";
    marker += file.hash;
    marker += '\t';
    marker += file.path;
    marker += '\n';
  }
  return write_file_bytes(
      cache_path / std::filesystem::path{std::string{CacheCompletionMarkerName}},
      std::span<const u8>{
          reinterpret_cast<const u8*>(marker.data()),
          marker.size()});
}

[[nodiscard]] PackBundleInstallResult install_decoded_bundle(
    DecodedBundle decoded,
    const std::filesystem::path& cache_root) {
  const std::filesystem::path final_path = client_pack_cache_path(cache_root, decoded.bundle_hash);
  if (client_pack_cache_contains(cache_root, decoded.bundle_hash)) {
    return PackBundleInstallResult::success(
        {std::move(decoded.bundle_hash), final_path, true});
  }

  const std::filesystem::path tmp_root =
      cache_root / "tmp" / (decoded.bundle_hash + ".partial");
  std::error_code error;
  std::filesystem::remove_all(tmp_root, error);
  error.clear();
  std::filesystem::create_directories(tmp_root, error);
  if (error) {
    return PackBundleInstallResult::failure(
        bundle_error("failed to create temporary client pack cache: " + error.message()));
  }

  for (const BundleFileEntry& file : decoded.files) {
    const std::filesystem::path output_path = tmp_root / std::filesystem::path{file.path};
    auto write = write_file_bytes(output_path, file.bytes);
    if (!write) {
      std::filesystem::remove_all(tmp_root, error);
      return PackBundleInstallResult::failure(write.error());
    }
  }

  auto marker = write_cache_marker(tmp_root, decoded.bundle_hash, decoded.files);
  if (!marker) {
    std::filesystem::remove_all(tmp_root, error);
    return PackBundleInstallResult::failure(marker.error());
  }

  std::filesystem::create_directories(final_path.parent_path(), error);
  if (error) {
    std::filesystem::remove_all(tmp_root, error);
    return PackBundleInstallResult::failure(
        bundle_error("failed to create client pack cache root: " + error.message()));
  }
  std::filesystem::rename(tmp_root, final_path, error);
  if (error) {
    if (client_pack_cache_contains(cache_root, decoded.bundle_hash)) {
      error.clear();
      std::filesystem::remove_all(tmp_root, error);
      return PackBundleInstallResult::success(
          {std::move(decoded.bundle_hash), final_path, true});
    }
    error.clear();
    std::filesystem::remove_all(final_path, error);
    error.clear();
    std::filesystem::rename(tmp_root, final_path, error);
    if (error) {
      std::filesystem::remove_all(tmp_root, error);
      return PackBundleInstallResult::failure(
          bundle_error("failed to finalize client pack cache entry"));
    }
  }
  return PackBundleInstallResult::success(
      {std::move(decoded.bundle_hash), final_path, false});
}

} // namespace

bool is_valid_bundle_hash(std::string_view hash) noexcept {
  if (hash.size() != BundleHashHexSize) {
    return false;
  }
  for (const char ch : hash) {
    const bool digit = ch >= '0' && ch <= '9';
    const bool lower_hex = ch >= 'a' && ch <= 'f';
    if (!digit && !lower_hex) {
      return false;
    }
  }
  return true;
}

std::filesystem::path client_pack_cache_path(
    const std::filesystem::path& cache_root,
    std::string_view bundle_hash) {
  return cache_root / std::filesystem::path{std::string{bundle_hash}};
}

bool client_pack_cache_contains(
    const std::filesystem::path& cache_root,
    std::string_view bundle_hash) {
  if (!is_valid_bundle_hash(bundle_hash)) {
    return false;
  }
  std::error_code error;
  const std::filesystem::path path = client_pack_cache_path(cache_root, bundle_hash);
  return std::filesystem::is_directory(path, error) &&
         std::filesystem::is_regular_file(path / "manifest.json", error) &&
         std::filesystem::is_regular_file(path / "registry-client.json", error) &&
         cache_marker_matches(path, bundle_hash);
}

bool is_client_bundle_relative_path_allowed(std::string_view path) noexcept {
  if (!safe_generic_path(path) || executable_extension(path)) {
    return false;
  }
  return path == "manifest.json" ||
         path == "registry-client.json" ||
         allowed_data_path(path) ||
         allowed_assets_path(path);
}

PackBundleResult clean_client_pack_cache_temporary_files(
    const std::filesystem::path& cache_root) {
  std::error_code error;
  std::filesystem::remove_all(cache_root / "tmp", error);
  if (error) {
    return PackBundleResult::failure(
        bundle_error("failed to clean temporary client pack cache: " + error.message()));
  }
  return PackBundleResult::success();
}

PackBundleInstallResult install_client_pack_bundle_from_file(
    const std::filesystem::path& apack_path,
    const std::filesystem::path& cache_root,
    std::string_view expected_bundle_hash,
    ClientPackBundleLimits limits) {
  if (!expected_bundle_hash.empty() &&
      client_pack_cache_contains(cache_root, expected_bundle_hash)) {
    return PackBundleInstallResult::success(
        {std::string{expected_bundle_hash},
         client_pack_cache_path(cache_root, expected_bundle_hash),
         true});
  }

  auto bytes = read_binary_file(apack_path, limits.max_compressed_size + 1024ULL * 1024ULL);
  if (!bytes) {
    return PackBundleInstallResult::failure(bytes.error());
  }
  auto decoded = decode_apack(bytes.value(), expected_bundle_hash, limits);
  if (!decoded) {
    return PackBundleInstallResult::failure(decoded.error());
  }
  return install_decoded_bundle(std::move(decoded.value()), cache_root);
}

} // namespace arv::asset
