#include "plugin_native_loading.hpp"

#include "arv/plugin/plugin_manager.hpp"
#include "plugin_paths.hpp"

namespace arv::plugin {

std::string current_native_platform_key() {
#if defined(_WIN32)
  constexpr std::string_view os = "windows";
#elif defined(__APPLE__)
  constexpr std::string_view os = "macos";
#elif defined(__linux__)
  constexpr std::string_view os = "linux";
#else
  constexpr std::string_view os = "unknown";
#endif

#if defined(__x86_64__) || defined(_M_X64)
  constexpr std::string_view arch = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  constexpr std::string_view arch = "arm64";
#else
  constexpr std::string_view arch = "unknown";
#endif
  return std::string{os} + "-" + std::string{arch};
}

bool native_library_extension_matches_platform(const std::filesystem::path& path) {
#if defined(_WIN32)
  return path.extension() == ".dll";
#elif defined(__APPLE__)
  return path.extension() == ".dylib";
#else
  return path.extension() == ".so";
#endif
}

namespace detail {

std::optional<std::filesystem::path> resolve_native_library_path(
    const asset::PackSource& pack,
    std::string_view platform_key,
    std::string& error) {
  if (!pack.manifest.native.has_value()) {
    error = "pack does not declare native metadata";
    return std::nullopt;
  }
  const auto found = pack.manifest.native->libraries.find(std::string{platform_key});
  if (found == pack.manifest.native->libraries.end()) {
    error = "native.libraries is missing current platform key: " +
            std::string{platform_key};
    return std::nullopt;
  }
  const std::filesystem::path path = (pack.root / found->second).lexically_normal();
  if (!path_is_under_lexically(pack.root, path)) {
    error = "native library path escapes pack root: " +
            found->second.generic_string();
    return std::nullopt;
  }
  if (!native_library_extension_matches_platform(path)) {
    error = "native library extension does not match platform: " +
            found->second.generic_string();
    return std::nullopt;
  }
  return path;
}

} // namespace detail
} // namespace arv::plugin
