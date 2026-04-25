#pragma once

#include <thread>

namespace arv::core {

enum class ThreadRole {
  Main,
  Simulation,
  Render,
  Worker,
  Network,
};

struct ThreadOwnership {
  std::thread::id main_thread{};
  std::thread::id simulation_thread{};
  std::thread::id render_thread{};

  static ThreadOwnership current_thread_owns_initial_roles() {
    const std::thread::id id = std::this_thread::get_id();
    return {id, id, id};
  }

  [[nodiscard]] bool is_main_thread() const noexcept {
    return std::this_thread::get_id() == main_thread;
  }

  [[nodiscard]] bool is_simulation_thread() const noexcept {
    return std::this_thread::get_id() == simulation_thread;
  }

  [[nodiscard]] bool is_render_thread() const noexcept {
    return std::this_thread::get_id() == render_thread;
  }
};

} // namespace arv::core
