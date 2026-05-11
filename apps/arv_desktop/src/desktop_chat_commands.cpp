#include "desktop_frame_loop.hpp"

#include <string>
#include <string_view>

namespace arv::desktop {
namespace {

[[nodiscard]] bool is_command_chat_draft(std::string_view draft) noexcept {
  return !draft.empty() && draft.front() == '/';
}

[[nodiscard]] std::string fallback_command_result_message(
    const lang::LanguageCatalog& language,
    command::CommandStatus status) {
  switch (status) {
    case command::CommandStatus::Success:
      return language.text("ui.chat.command.success");
    case command::CommandStatus::Failure:
      return language.text("ui.chat.command.failure");
    case command::CommandStatus::UsageError:
      return language.text("ui.chat.command.usage_error");
    case command::CommandStatus::PermissionDenied:
      return language.text("ui.chat.command.permission_denied");
    case command::CommandStatus::UnknownCommand:
      return language.text("ui.chat.command.unknown_command");
    case command::CommandStatus::InvalidSource:
      return language.text("ui.chat.command.invalid_source");
    case command::CommandStatus::InternalError:
      return language.text("ui.chat.command.internal_error");
  }
  return language.text("ui.chat.command.failure");
}

void append_command_result_to_chat(
    client::ClientRuntime& client,
    const command::CommandResult& result,
    const lang::LanguageCatalog& language) {
  bool wrote_message = false;
  for (const command::CommandMessage& message : result.messages) {
    if (message.text.empty()) {
      continue;
    }
    client.add_chat_message(message.text);
    wrote_message = true;
  }
  if (!wrote_message) {
    client.add_chat_message(fallback_command_result_message(language, result.status));
  }
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

  const std::string command_text = active_session->client->chat_draft();
  if (!is_command_chat_draft(command_text)) {
    return;
  }

  client_input.text_submit_pressed = false;
  active_session->client->close_chat();
  append_command_result_to_chat(
      *active_session->client,
      execute_chat_command(*active_session, command_text),
      assets_.language);
}

} // namespace arv::desktop
