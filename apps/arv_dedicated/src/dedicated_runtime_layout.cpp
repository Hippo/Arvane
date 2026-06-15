#include "dedicated_runtime_layout.hpp"

#include "arv/core/log.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>

namespace arv::dedicated {
namespace {

[[nodiscard]] Expected<void, DedicatedError> ensure_directory(
    const std::filesystem::path& path,
    std::string_view label) {
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error(
            "runtime-layout",
            "failed to create " + std::string{label} + " directory '" +
                path.string() + "': " + error.message()));
  }
  return Expected<void, DedicatedError>::success();
}

[[nodiscard]] bool regular_file_exists(const std::filesystem::path& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

[[nodiscard]] Expected<void, DedicatedError> copy_directory_tree(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
  auto create_root = ensure_directory(destination, "seeded pack");
  if (!create_root) {
    return create_root;
  }

  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(source, error);
  if (error) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error(
            "runtime-layout",
            "failed to read default pack directory '" + source.string() +
                "': " + error.message()));
  }

  const std::filesystem::recursive_directory_iterator end;
  for (; iterator != end; iterator.increment(error)) {
    if (error) {
      return Expected<void, DedicatedError>::failure(
          dedicated_error(
              "runtime-layout",
              "failed while copying default pack from '" + source.string() +
                  "': " + error.message()));
    }

    const std::filesystem::directory_entry& entry = *iterator;
    const std::filesystem::path relative = entry.path().lexically_relative(source);
    const std::filesystem::path target = destination / relative;
    if (entry.is_directory(error)) {
      auto create_child = ensure_directory(target, "seeded pack");
      if (!create_child) {
        return create_child;
      }
      error.clear();
      continue;
    }
    if (error) {
      return Expected<void, DedicatedError>::failure(
          dedicated_error(
              "runtime-layout",
              "failed to inspect default pack entry '" + entry.path().string() +
                  "': " + error.message()));
    }
    if (!entry.is_regular_file(error)) {
      return Expected<void, DedicatedError>::failure(
          dedicated_error(
              "runtime-layout",
              "unsupported default pack entry type: " + entry.path().string()));
    }
    error.clear();

    auto create_parent = ensure_directory(target.parent_path(), "seeded pack");
    if (!create_parent) {
      return create_parent;
    }
    std::filesystem::copy_file(
        entry.path(),
        target,
        std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) {
      return Expected<void, DedicatedError>::failure(
          dedicated_error(
              "runtime-layout",
              "failed to copy default pack file '" + entry.path().string() +
                  "' to '" + target.string() + "': " + error.message()));
    }
  }

  return Expected<void, DedicatedError>::success();
}

[[nodiscard]] Expected<void, DedicatedError> seed_base_pack_if_needed(
    const io::EngineConfig& config) {
  const std::filesystem::path& packs_root = config.paths.server_packs_path();
  std::error_code error;
  if (std::filesystem::is_directory(packs_root, error)) {
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(packs_root, error)) {
      if (error) {
        error.clear();
        continue;
      }
      if (entry.is_directory(error) && regular_file_exists(entry.path() / "pack.yaml")) {
        return Expected<void, DedicatedError>::success();
      }
      error.clear();
    }
  }

  const std::filesystem::path source_base =
      config.paths.packs_path() / "arvane_base";
  if (!regular_file_exists(source_base / "pack.yaml")) {
    return Expected<void, DedicatedError>::failure(
        dedicated_error(
            "runtime-layout",
            "server packs are not configured and the default base pack was not "
            "found at " +
                (source_base / "pack.yaml").string()));
  }

  const std::filesystem::path destination_base = packs_root / "arvane_base";
  auto copy_result = copy_directory_tree(source_base, destination_base);
  if (!copy_result) {
    return copy_result;
  }
  core::info(
      "dedicated",
      "seeded default server pack '{}' from '{}'",
      destination_base.string(),
      source_base.string());
  return Expected<void, DedicatedError>::success();
}

} // namespace

Expected<void, DedicatedError> ensure_dedicated_runtime_layout(
    const io::EngineConfig& config) {
  for (const auto& [path, label] : {
           std::pair{config.paths.server_root_path(), std::string_view{"server root"}},
           std::pair{config.paths.server_packs_path(), std::string_view{"server packs"}},
           std::pair{config.paths.server_worlds_path(), std::string_view{"server worlds"}},
           std::pair{config.paths.server_cache_path(), std::string_view{"server cache"}},
           std::pair{config.paths.server_logs_path(), std::string_view{"server logs"}},
       }) {
    auto create_result = ensure_directory(path, label);
    if (!create_result) {
      return create_result;
    }
  }

  return seed_base_pack_if_needed(config);
}

} // namespace arv::dedicated
