#pragma once

#include "arv/command/command.hpp"

namespace arv::server {

[[nodiscard]] command::CommandSender server_console_command_sender();

} // namespace arv::server
