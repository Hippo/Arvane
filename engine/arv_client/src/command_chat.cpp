#include "arv/client/command_chat.hpp"

#include "arv/client/client_runtime.hpp"
#include "arv/command/command.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/text/chat_message.hpp"
#include "arv/text/language.hpp"

#include <string>
#include <string_view>

namespace arv::client {
namespace {

[[nodiscard]] std::string fallback_command_result_message(
    const text::LanguageCatalog& language,
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

[[nodiscard]] bool command_message_is_error(
    command::CommandMessageKind kind) noexcept {
  return kind == command::CommandMessageKind::Error;
}

[[nodiscard]] bool command_status_is_error(
    command::CommandStatus status) noexcept {
  return status != command::CommandStatus::Success;
}

[[nodiscard]] protocol::ClientboundChatMessage command_chat_packet(
    std::string_view message,
    bool error) {
  protocol::ClientboundChatMessage packet;
  packet.message_id = 0U;
  packet.channel = std::string{protocol::ChatChannelSystem};
  packet.kind = error ? protocol::ChatMessageKind::Error : protocol::ChatMessageKind::System;
  packet.message = text::ChatMessageTemplate::parse(message).to_rich_text();
  return packet;
}

} // namespace

void append_command_result_to_chat(
    ClientRuntime& client,
    const command::CommandResult& result,
    const text::LanguageCatalog& language) {
  bool wrote_message = false;
  for (const command::CommandMessage& message : result.messages) {
    if (message.text.empty()) {
      continue;
    }
    client.add_chat_message(command_chat_packet(
        message.text,
        command_message_is_error(message.kind)));
    wrote_message = true;
  }
  if (!wrote_message && result.status != command::CommandStatus::PermissionDenied) {
    client.add_chat_message(command_chat_packet(
        fallback_command_result_message(language, result.status),
        command_status_is_error(result.status)));
  }
}

} // namespace arv::client
