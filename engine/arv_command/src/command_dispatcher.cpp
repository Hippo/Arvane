#include "arv/command/command_dispatcher.hpp"

#include <cctype>
#include <string>

namespace arv::command {
namespace {

[[nodiscard]] bool ascii_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

[[nodiscard]] std::string_view trim_ascii(std::string_view text) noexcept {
  while (!text.empty() && ascii_space(text.front())) {
    text.remove_prefix(1U);
  }
  while (!text.empty() && ascii_space(text.back())) {
    text.remove_suffix(1U);
  }
  return text;
}

[[nodiscard]] CommandResult validate_access(
    const Command& command,
    const CommandSender& sender,
    CommandServices& services) {
  if (command.console_only && sender.kind != CommandSourceKind::ServerConsole) {
    return CommandResult::invalid_source("Command is restricted to the server console.");
  }

  if (command.domain != CommandDomain::ServerBound ||
      sender.kind != CommandSourceKind::Player) {
    return CommandResult::success();
  }

  if (command.public_to_players || command.permission.empty()) {
    return CommandResult::success();
  }

  if (!services.permission_check) {
    return CommandResult::internal_error("Command permission service is unavailable.");
  }

  const CommandPermissionResult permission = services.permission_check(sender, command);
  if (permission.allowed) {
    return CommandResult::success();
  }

  if (!permission.message.empty()) {
    return CommandResult::permission_denied(permission.message);
  }

  std::string text{"Permission denied: "};
  text.append(command.permission);
  text.push_back('.');
  return CommandResult::permission_denied(std::move(text));
}

} // namespace

std::string normalize_command_text(std::string_view command_text) {
  std::string_view trimmed = trim_ascii(command_text);
  if (!trimmed.empty() && trimmed.front() == '/') {
    trimmed.remove_prefix(1U);
    trimmed = trim_ascii(trimmed);
  }
  return std::string{trimmed};
}

std::vector<std::string_view> tokenize_command_text(std::string_view normalized_command_text) {
  std::vector<std::string_view> tokens;
  usize cursor = 0U;
  while (cursor < normalized_command_text.size()) {
    while (cursor < normalized_command_text.size() &&
           ascii_space(normalized_command_text[cursor])) {
      ++cursor;
    }
    if (cursor >= normalized_command_text.size()) {
      break;
    }

    const usize start = cursor;
    while (cursor < normalized_command_text.size() &&
           !ascii_space(normalized_command_text[cursor])) {
      ++cursor;
    }
    tokens.emplace_back(normalized_command_text.data() + start, cursor - start);
  }
  return tokens;
}

CommandResult dispatch_command(
    const CommandRegistry& registry,
    CommandDomain domain,
    const CommandSender& sender,
    CommandServices& services,
    std::string_view command_text) {
  const std::string normalized = normalize_command_text(command_text);
  const std::vector<std::string_view> tokens = tokenize_command_text(normalized);
  if (tokens.empty()) {
    return CommandResult::usage_error("No command entered.");
  }

  const std::string_view input_name = tokens.front();
  const std::optional<CommandLookup> lookup = registry.resolve(input_name);
  if (!lookup.has_value() || lookup->command == nullptr) {
    std::string text{"Unknown command: "};
    text.append(input_name);
    text.push_back('.');
    return CommandResult::unknown_command(std::move(text));
  }

  const Command& command = *lookup->command;
  if (command.domain != domain) {
    std::string text{"Command is not available in this domain: "};
    text.append(input_name);
    text.push_back('.');
    return CommandResult::invalid_source(std::move(text));
  }

  CommandResult access = validate_access(command, sender, services);
  if (!access) {
    return access;
  }

  if (!command.execute) {
    std::string text{"Command has no executor: "};
    text.append(command.name);
    text.push_back('.');
    return CommandResult::internal_error(std::move(text));
  }

  CommandArgs args;
  if (tokens.size() > 1U) {
    args.tokens.assign(tokens.begin() + 1, tokens.end());
  }

  CommandCtx ctx{command, sender, args, services};
  return command.execute(ctx);
}

} // namespace arv::command
