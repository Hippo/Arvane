#include "arv/core/thread_roles.hpp"

namespace arv::core {

ThreadOwnership ThreadOwnership::current_thread_owns_initial_roles() {
  const std::thread::id id = std::this_thread::get_id();
  return {id, id, id};
}

bool ThreadOwnership::is_main_thread() const noexcept {
  return std::this_thread::get_id() == main_thread;
}

bool ThreadOwnership::is_simulation_thread() const noexcept {
  return std::this_thread::get_id() == simulation_thread;
}

bool ThreadOwnership::is_render_thread() const noexcept {
  return std::this_thread::get_id() == render_thread;
}

} // namespace arv::core
