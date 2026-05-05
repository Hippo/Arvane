#pragma once

#include "arv/command/command.hpp"
#include "arv/command/command_registry.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace arv::command {

[[nodiscard]] std::string normalize_command_text(std::string_view command_text);

[[nodiscard]] std::vector<std::string_view> tokenize_command_text(
    std::string_view normalized_command_text);

[[nodiscard]] CommandResult dispatch_command(
    const CommandRegistry& registry,
    CommandDomain domain,
    const CommandSender& sender,
    CommandServices& services,
    std::string_view command_text);

} // namespace arv::command
