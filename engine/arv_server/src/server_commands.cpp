#include "arv/server/server_commands.hpp"

namespace arv::server {

command::CommandSender server_console_command_sender() {
  command::CommandSender sender;
  sender.kind = command::CommandSourceKind::ServerConsole;
  sender.display_name = "server_console";
  return sender;
}

} // namespace arv::server
