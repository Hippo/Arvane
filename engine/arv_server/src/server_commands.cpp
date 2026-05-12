#include "arv/server/server_commands.hpp"

#include "arv/base/base_commands.hpp"

namespace arv::server {

void register_server_commands(command::CommandRegistry& registry) {
  base::register_server_commands(registry);
}

command::CommandSender server_console_command_sender() {
  command::CommandSender sender;
  sender.kind = command::CommandSourceKind::ServerConsole;
  sender.display_name = "server_console";
  return sender;
}

} // namespace arv::server
