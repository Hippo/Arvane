#include "arv/base/base_commands.hpp"

#include "tp_command.hpp"

namespace arv::base {

void register_server_commands(command::CommandRegistry& registry) {
  commands::register_tp_command(registry);
}

} // namespace arv::base
