#pragma once

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace arv::testsupport {

class ScopedTempDir {
public:
  std::filesystem::path path;

  explicit ScopedTempDir(std::string_view prefix = "arvane_test_")
      : path(std::filesystem::temp_directory_path() /
             (std::string{prefix} +
              std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
    std::error_code error;
    std::filesystem::remove_all(path, error);
    std::filesystem::create_directories(path);
  }

  ~ScopedTempDir() {
    if (path.empty()) {
      return;
    }
    std::error_code error;
    std::filesystem::remove_all(path, error);
  }

  ScopedTempDir(const ScopedTempDir&) = delete;
  ScopedTempDir& operator=(const ScopedTempDir&) = delete;

  ScopedTempDir(ScopedTempDir&& other) noexcept : path(std::move(other.path)) {
    other.path.clear();
  }

  ScopedTempDir& operator=(ScopedTempDir&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    std::error_code error;
    std::filesystem::remove_all(path, error);
    path = std::move(other.path);
    other.path.clear();
    return *this;
  }
};

using TempDir = ScopedTempDir;

} // namespace arv::testsupport
