#include "dedicated_tick_loop.hpp"

#include "dedicated_signals.hpp"

#include "arv/command/command.hpp"
#include "arv/core/fixed_timestep.hpp"
#include "arv/core/log.hpp"
#include "arv/core/string.hpp"
#include "arv/sched/owner_thread.hpp"
#include "arv/server/server_commands.hpp"

#include <chrono>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

#if defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <unistd.h>
#endif

namespace arv::dedicated {
namespace {

class DedicatedConsoleInput {
public:
  [[nodiscard]] std::optional<std::string> poll_line() {
    if (closed_) {
      return std::nullopt;
    }

#if defined(__unix__) || defined(__APPLE__)
    pollfd stdin_poll{};
    stdin_poll.fd = STDIN_FILENO;
    stdin_poll.events = POLLIN | POLLHUP | POLLERR;
    const int ready = ::poll(&stdin_poll, 1, 0);
    if (ready <= 0) {
      return std::nullopt;
    }
    if ((stdin_poll.revents & POLLIN) == 0) {
      if ((stdin_poll.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0) {
        closed_ = true;
      }
      return std::nullopt;
    }

    std::string line;
    if (!std::getline(std::cin, line)) {
      closed_ = true;
      return std::nullopt;
    }
    return line;
#else
    return std::nullopt;
#endif
  }

private:
  bool closed_{};
};

void log_console_command_result(const command::CommandResult& result) {
  bool wrote_message = false;
  for (const command::CommandMessage& message : result.messages) {
    if (message.text.empty()) {
      continue;
    }
    wrote_message = true;
    if (message.kind == command::CommandMessageKind::Error) {
      core::warn("console", message.text);
    } else {
      core::info("console", message.text);
    }
  }

  if (!wrote_message && result.status != command::CommandStatus::Success) {
    core::warn("console", "Command failed without a message.");
  }
}

void drain_console_commands(server::ServerRuntime& server, DedicatedConsoleInput& console) {
  for (;;) {
    std::optional<std::string> line = console.poll_line();
    if (!line.has_value()) {
      return;
    }

    const std::string command_text = trim_ascii_copy(line.value());
    if (command_text.empty()) {
      continue;
    }

    const command::CommandResult result =
        server.execute_command(server::server_console_command_sender(), command_text);
    log_console_command_result(result);
  }
}

} // namespace

DedicatedTickLoopResult run_dedicated_tick_loop(
    server::ServerRuntime& server,
    u32 base_tps,
    u64 max_ticks) {
  sched::WorldThread world_thread;
  (void)world_thread.start();
  core::info("threading", "world_owner=world thread");
  DedicatedTickLoopResult result = world_thread.call([&server, base_tps, max_ticks] {
    server.voxel_world().bind_owner_thread();
    const auto sleep_duration = std::chrono::duration<Seconds>(
        tick_duration_seconds(base_tps));

    DedicatedTickLoopResult thread_result;
    DedicatedConsoleInput console;
    while (!shutdown_requested() &&
           (max_ticks == 0 || thread_result.ticks_run < max_ticks)) {
      drain_console_commands(server, console);
      server.tick();
      ++thread_result.ticks_run;
      std::this_thread::sleep_for(sleep_duration);
    }

    thread_result.stopped_by_shutdown_request = shutdown_requested();
    thread_result.stopped_by_tick_limit =
        max_ticks != 0 && thread_result.ticks_run >= max_ticks;
    server.shutdown();
    return thread_result;
  });
  world_thread.request_stop();
  world_thread.join();
  return result;
}

} // namespace arv::dedicated
