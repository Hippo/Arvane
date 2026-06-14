#include "ui_model.hpp"

#include <chrono>
#include <optional>
#include <utility>

namespace arv::desktop {

std::vector<ui::PlayerListEntryView> player_list_for_ui(
    const server::ServerRuntime& server) {
  std::vector<ui::PlayerListEntryView> players;
  players.reserve(server.sessions().size());
  server.sessions().for_each_session([&players](const server::ServerSession& session) {
    players.emplace_back(session.profile_name, std::nullopt);
  });
  return players;
}

usize chat_history_max_scroll_offset(
    usize history_size,
    usize max_visible_messages) noexcept {
  return client::gameplay_chat_history_max_scroll_offset(
      history_size,
      max_visible_messages);
}

usize chat_history_scroll_steps(f32 wheel_y) noexcept {
  return client::gameplay_chat_scroll_steps(wheel_y);
}

void refresh_gameplay_chat_lines_for_ui(
    std::vector<ui::ChatLineView>& chat_lines,
    const client::ClientRuntime& client_runtime,
    const io::UiConfig::ChatConfig& chat_config,
    std::chrono::steady_clock::time_point now,
    usize& history_scroll_offset) {
  client::refresh_gameplay_chat_lines(
      chat_lines,
      client_runtime,
      chat_config,
      now,
      history_scroll_offset);
}

ui::GameplayUiModel gameplay_ui_model_for_ui(
    client::GameplayUiModelInput input) {
  return client::build_gameplay_ui_model(std::move(input));
}

} // namespace arv::desktop
