#pragma once

#include "arv/command/command.hpp"
#include "arv/command/command_registry.hpp"

namespace arv::server {

void register_server_commands(command::CommandRegistry& registry);

[[nodiscard]] command::CommandSender server_console_command_sender();

} // namespace arv::server
