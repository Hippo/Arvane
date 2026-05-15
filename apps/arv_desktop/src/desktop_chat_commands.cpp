#include "desktop_frame_loop.hpp"

#include "arv/client/command_chat.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace arv::desktop {
namespace {

[[nodiscard]] bool is_command_chat_draft(std::string_view draft) noexcept {
  return !draft.empty() && draft.front() == '/';
}

} // namespace

command::CommandResult DesktopFrameLoop::execute_chat_command(
    DesktopGameSession& session,
    std::string_view command_text) {
  if (session.integrated_server == nullptr) {
    return command::CommandResult::invalid_source(
        assets_.language.text("ui.chat.command.remote_unavailable"));
  }

  const server::ServerSession* server_session =
      session.integrated_server->sessions().first();
  if (server_session == nullptr) {
    return command::CommandResult::invalid_source(
        assets_.language.text("ui.chat.command.no_player_session"));
  }

  std::optional<command::CommandSender> sender =
      session.integrated_server->command_sender_for_session(server_session->session_id);
  if (!sender.has_value()) {
    return command::CommandResult::invalid_source(
        assets_.language.text("ui.chat.command.no_sender"));
  }

  return session.integrated_server->execute_command(sender.value(), command_text);
}

void DesktopFrameLoop::handle_chat_command_submit(
    DesktopGameSession* active_session,
    bool has_active_session,
    client::InputFrame& client_input) {
  if (!has_active_session ||
      active_session == nullptr ||
      active_session->client == nullptr ||
      !active_session->client->chat_open() ||
      !client_input.text_submit_pressed) {
    return;
  }

  if (!is_command_chat_draft(active_session->client->chat_draft())) {
    return;
  }

  std::optional<std::string> command_text =
      active_session->client->commit_chat_draft_to_history();
  client_input.text_submit_pressed = false;
  active_session->client->close_chat();
  if (!command_text.has_value()) {
    return;
  }
  client::append_command_result_to_chat(
      *active_session->client,
      execute_chat_command(*active_session, command_text.value()),
      assets_.language);
}

} // namespace arv::desktop
