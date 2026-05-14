#pragma once

#include "arv/asset/pack.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace arv::plugin {

struct PluginError {
  std::string pack_id{};
  std::string phase{};
  std::filesystem::path file{};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

using PluginResult = Expected<void, PluginError>;

enum class PluginState : u8 {
  Discovered,
  SkippedByPolicy,
  Loaded,
  Initialized,
  Failed,
  ShuttingDown,
  Shutdown,
};

struct PluginInfo {
  std::string pack_id{};
  std::string pack_version{};
  std::vector<std::string> namespaces{};
  asset::PackSide side{asset::PackSide::Both};
  PluginState state{PluginState::Discovered};
  std::filesystem::path library_path{};
  std::string entrypoint{};
  std::vector<std::string> diagnostics{};
};

[[nodiscard]] const char* plugin_state_name(PluginState state) noexcept;

} // namespace arv::plugin
