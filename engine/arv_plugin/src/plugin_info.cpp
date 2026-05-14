#include "arv/plugin/plugin_info.hpp"

namespace arv::plugin {

std::string PluginError::describe() const {
  std::string text;
  if (!phase.empty()) {
    text += phase;
    text += ": ";
  }
  text += message;
  if (!pack_id.empty()) {
    text += " (pack ";
    text += pack_id;
    text += ")";
  }
  if (!file.empty()) {
    text += " [file ";
    text += file.generic_string();
    text += "]";
  }
  return text;
}

const char* plugin_state_name(PluginState state) noexcept {
  switch (state) {
    case PluginState::Discovered:
      return "Discovered";
    case PluginState::SkippedByPolicy:
      return "SkippedByPolicy";
    case PluginState::Loaded:
      return "Loaded";
    case PluginState::Initialized:
      return "Initialized";
    case PluginState::Failed:
      return "Failed";
    case PluginState::ShuttingDown:
      return "ShuttingDown";
    case PluginState::Shutdown:
      return "Shutdown";
  }
  return "Unknown";
}

} // namespace arv::plugin
