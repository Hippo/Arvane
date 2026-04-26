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

  static ThreadOwnership current_thread_owns_initial_roles();

  [[nodiscard]] bool is_main_thread() const noexcept;

  [[nodiscard]] bool is_simulation_thread() const noexcept;

  [[nodiscard]] bool is_render_thread() const noexcept;
};

} // namespace arv::core
