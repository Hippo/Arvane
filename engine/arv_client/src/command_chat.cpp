#include "arv/client/command_chat.hpp"

#include "arv/client/client_runtime.hpp"
#include "arv/command/command.hpp"
#include "arv/text/language.hpp"

#include <string>

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
    client.add_chat_message(message.text);
    wrote_message = true;
  }
  if (!wrote_message) {
    client.add_chat_message(fallback_command_result_message(language, result.status));
  }
}

} // namespace arv::client
