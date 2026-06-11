#include "arv/command/command_dispatcher.hpp"

#include "arv/core/string.hpp"
#include "arv/text/language.hpp"

#include <string>

namespace arv::command {
namespace {

[[nodiscard]] std::string localized_command_text(
    const CommandServices& services,
    std::string_view key,
    std::string_view fallback) {
  return text::text_or_fallback(services.language, key, fallback);
}

[[nodiscard]] std::string command_format(
    const CommandServices& services,
    std::string_view key,
    std::initializer_list<arv::text::FormatArg> args,
    std::string_view fallback) {
  return text::format_or_fallback(services.language, key, args, fallback);
}

[[nodiscard]] CommandResult validate_access(
    const Command& command,
    const CommandSender& sender,
    CommandServices& services) {
  if (command.console_only && sender.kind != CommandSourceKind::ServerConsole) {
    return CommandResult::invalid_source(localized_command_text(
        services,
        "command.dispatcher.console_only",
        "Command is restricted to the server console."));
  }

  if (command.domain != CommandDomain::ServerBound ||
      sender.kind != CommandSourceKind::Player) {
    return CommandResult::success();
  }

  if (command.public_to_players || command.permission.empty()) {
    return CommandResult::success();
  }

  if (!services.permission_check) {
    return CommandResult::internal_error(localized_command_text(
        services,
        "command.dispatcher.permission_service_unavailable",
        "Command permission service is unavailable."));
  }

  const CommandPermissionResult permission = services.permission_check(sender, command);
  if (permission.allowed) {
    return CommandResult::success();
  }

  if (permission.has_message) {
    return CommandResult::permission_denied(permission.message);
  }

  return CommandResult::permission_denied(command_format(
      services,
      "command.dispatcher.permission_denied",
      {{"permission", command.permission}},
      "Permission denied: {permission}."));
}

[[nodiscard]] bool has_unclosed_quote(std::string_view text) noexcept {
  bool quoted = false;
  for (const char ch : text) {
    if (ch == '"') {
      quoted = !quoted;
    }
  }
  return quoted;
}

} // namespace

std::string normalize_command_text(std::string_view command_text) {
  std::string_view trimmed = trim_ascii_view(command_text);
  if (!trimmed.empty() && trimmed.front() == '/') {
    trimmed.remove_prefix(1U);
    trimmed = trim_ascii_view(trimmed);
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

    if (normalized_command_text[cursor] == '"') {
      ++cursor;
      const usize start = cursor;
      while (cursor < normalized_command_text.size() &&
             normalized_command_text[cursor] != '"') {
        ++cursor;
      }
      tokens.emplace_back(normalized_command_text.data() + start, cursor - start);
      if (cursor < normalized_command_text.size()) {
        ++cursor;
      }
      continue;
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
  services.registry = &registry;
  if (has_unclosed_quote(normalized)) {
    return CommandResult::usage_error(localized_command_text(
        services,
        "command.dispatcher.unclosed_quote",
        "Unclosed quoted string."));
  }
  const std::vector<std::string_view> tokens = tokenize_command_text(normalized);
  if (tokens.empty()) {
    return CommandResult::usage_error(localized_command_text(
        services,
        "command.dispatcher.no_command",
        "No command entered."));
  }

  const std::string_view input_name = tokens.front();
  const std::optional<CommandLookup> lookup = registry.resolve(input_name);
  if (!lookup.has_value() || lookup->command == nullptr) {
    return CommandResult::unknown_command(command_format(
        services,
        "command.dispatcher.unknown_command",
        {{"command", std::string{input_name}}},
        "Unknown command: {command}."));
  }

  const Command& command = *lookup->command;
  if (command.domain != domain) {
    return CommandResult::invalid_source(command_format(
        services,
        "command.dispatcher.wrong_domain",
        {{"command", std::string{input_name}}},
        "Command is not available in this domain: {command}."));
  }

  CommandResult access = validate_access(command, sender, services);
  if (!access) {
    return access;
  }

  if (!command.execute) {
    return CommandResult::internal_error(command_format(
        services,
        "command.dispatcher.no_executor",
        {{"command", command.name}},
        "Command has no executor: {command}."));
  }

  CommandArgs args;
  if (tokens.size() > 1U) {
    args.tokens.assign(tokens.begin() + 1, tokens.end());
  }

  CommandCtx ctx{command, sender, args, services};
  return command.execute(ctx);
}

} // namespace arv::command
