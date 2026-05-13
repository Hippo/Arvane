#pragma once

#include "arv/plugin/plugin_manager.hpp"

#include <filesystem>
#include <string>

namespace arv::plugin::detail {

class DynamicLibrary {
public:
  DynamicLibrary() = default;
  ~DynamicLibrary();

  DynamicLibrary(DynamicLibrary&& other) noexcept;
  DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

  DynamicLibrary(const DynamicLibrary&) = delete;
  DynamicLibrary& operator=(const DynamicLibrary&) = delete;

  [[nodiscard]] static Expected<DynamicLibrary, PluginError> load(
      const std::filesystem::path& path,
      std::string pack_id);

  [[nodiscard]] void* symbol(std::string_view name, std::string& error_out) const noexcept;
  [[nodiscard]] bool valid() const noexcept {
    return handle_ != nullptr;
  }

private:
  explicit DynamicLibrary(void* handle) noexcept : handle_(handle) {}

  void close() noexcept;

  void* handle_{};
};

} // namespace arv::plugin::detail
