#include "arv/command/command_registry.hpp"

#include "arv/core/string.hpp"

#include <utility>

namespace arv::command {
namespace {

[[nodiscard]] CommandRegistry::Result registry_error(std::string message) {
  return CommandRegistry::Result::failure(CommandRegistryError{std::move(message)});
}

} // namespace

CommandRegistry::Result CommandRegistry::register_command(Command command) {
  if (command.name.empty() || contains_ascii_space(command.name)) {
    return registry_error("Command name must be non-empty and contain no whitespace.");
  }
  if (command_indices_.contains(command.name) || alias_indices_.contains(command.name)) {
    std::string text{"Duplicate command name: "};
    text.append(command.name);
    text.push_back('.');
    return registry_error(std::move(text));
  }

  const usize index = commands_.size();
  command_indices_[command.name] = index;
  commands_.push_back(std::move(command));
  return Result::success();
}

CommandRegistry::Result CommandRegistry::register_alias(
    std::string alias,
    std::string command_name,
    bool visible_in_help) {
  if (alias.empty() || contains_ascii_space(alias)) {
    return registry_error("Command alias must be non-empty and contain no whitespace.");
  }
  if (command_indices_.contains(alias) || alias_indices_.contains(alias)) {
    std::string text{"Duplicate command alias: "};
    text.append(alias);
    text.push_back('.');
    return registry_error(std::move(text));
  }
  if (!command_indices_.contains(command_name)) {
    std::string text{"Alias target command does not exist: "};
    text.append(command_name);
    text.push_back('.');
    return registry_error(std::move(text));
  }
  if (alias == command_name) {
    return registry_error("Command alias cannot target itself.");
  }

  const usize index = aliases_.size();
  alias_indices_[alias] = index;
  const std::string alias_copy = alias;
  aliases_.push_back(CommandAlias{
      std::move(alias),
      std::move(command_name),
      visible_in_help,
  });
  if (visible_in_help) {
    const auto command_found = command_indices_.find(aliases_.back().command_name);
    if (command_found != command_indices_.end()) {
      commands_[command_found->second].aliases.push_back(alias_copy);
    }
  }
  return Result::success();
}

const Command* CommandRegistry::find(std::string_view name) const noexcept {
  const auto found = command_indices_.find(std::string{name});
  if (found == command_indices_.end()) {
    return nullptr;
  }
  return &commands_[found->second];
}

std::optional<CommandLookup> CommandRegistry::resolve(std::string_view name) const noexcept {
  if (const Command* command = find(name); command != nullptr) {
    return CommandLookup{command, command->name, command->name, false};
  }

  const auto found_alias = alias_indices_.find(std::string{name});
  if (found_alias == alias_indices_.end()) {
    return std::nullopt;
  }

  const CommandAlias& alias = aliases_[found_alias->second];
  const Command* command = find(alias.command_name);
  if (command == nullptr) {
    return std::nullopt;
  }

  return CommandLookup{command, alias.alias, command->name, true};
}

std::vector<const Command*> CommandRegistry::commands() const {
  std::vector<const Command*> out;
  out.reserve(commands_.size());
  for (const Command& command : commands_) {
    out.push_back(&command);
  }
  return out;
}

std::vector<CommandAlias> CommandRegistry::aliases() const {
  return aliases_;
}

std::vector<std::string> CommandRegistry::visible_aliases_for(
    std::string_view command_name) const {
  std::vector<std::string> out;
  for (const CommandAlias& alias : aliases_) {
    if (alias.visible_in_help && alias.command_name == command_name) {
      out.push_back(alias.alias);
    }
  }
  return out;
}

} // namespace arv::command
