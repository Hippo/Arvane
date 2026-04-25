#pragma once

#include <string_view>

namespace arv::server {

enum class DedicatedServerLifetimeState {
  Starting,
  LoadingConfig,
  LoadingRegistries,
  LoadingWorlds,
  Running,
  Stopping,
  Saving,
  Stopped,
  Failed,
};

[[nodiscard]] constexpr std::string_view to_string(DedicatedServerLifetimeState state) noexcept {
  switch (state) {
  case DedicatedServerLifetimeState::Starting:
    return "starting";
  case DedicatedServerLifetimeState::LoadingConfig:
    return "loading_config";
  case DedicatedServerLifetimeState::LoadingRegistries:
    return "loading_registries";
  case DedicatedServerLifetimeState::LoadingWorlds:
    return "loading_worlds";
  case DedicatedServerLifetimeState::Running:
    return "running";
  case DedicatedServerLifetimeState::Stopping:
    return "stopping";
  case DedicatedServerLifetimeState::Saving:
    return "saving";
  case DedicatedServerLifetimeState::Stopped:
    return "stopped";
  case DedicatedServerLifetimeState::Failed:
    return "failed";
  }
  return "unknown";
}

class DedicatedServerLifetime {
public:
  void transition(DedicatedServerLifetimeState state) noexcept {
    state_ = state;
  }

  [[nodiscard]] DedicatedServerLifetimeState state() const noexcept {
    return state_;
  }

private:
  DedicatedServerLifetimeState state_{DedicatedServerLifetimeState::Starting};
};

} // namespace arv::server
