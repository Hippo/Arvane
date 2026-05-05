#pragma once

#include "arv/command/command.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::command {

struct CommandRegistryError {
  std::string message{};
};

struct CommandAlias {
  std::string alias{};
  std::string command_name{};
  bool visible_in_help{};
};

struct CommandLookup {
  const Command* command{};
  std::string_view input_name{};
  std::string_view canonical_name{};
  bool used_alias{};
};

class CommandRegistry {
public:
  using Result = Expected<void, CommandRegistryError>;

  [[nodiscard]] Result register_command(Command command);
  [[nodiscard]] Result register_alias(
      std::string alias,
      std::string command_name,
      bool visible_in_help = false);

  [[nodiscard]] const Command* find(std::string_view name) const noexcept;
  [[nodiscard]] std::optional<CommandLookup> resolve(std::string_view name) const noexcept;

  [[nodiscard]] std::vector<const Command*> commands() const;
  [[nodiscard]] std::vector<CommandAlias> aliases() const;

private:
  std::vector<Command> commands_{};
  HashMap<std::string, usize> command_indices_{};
  HashMap<std::string, usize> alias_indices_{};
  std::vector<CommandAlias> aliases_{};
};

} // namespace arv::command
