#include "dedicated_signals.hpp"

#include <atomic>
#include <csignal>

namespace arv::dedicated {
namespace {

std::atomic_bool g_shutdown_requested{false};

void handle_shutdown_signal(int) {
  request_shutdown();
}

} // namespace

void install_shutdown_signal_handlers() {
  g_shutdown_requested.store(false);
  std::signal(SIGINT, handle_shutdown_signal);
  std::signal(SIGTERM, handle_shutdown_signal);
}

bool shutdown_requested() noexcept {
  return g_shutdown_requested.load();
}

void request_shutdown() noexcept {
  g_shutdown_requested.store(true);
}

} // namespace arv::dedicated
