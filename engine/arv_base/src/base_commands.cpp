#include "arv/base/base_commands.hpp"

#include "blueprint_command.hpp"
#include "selection_command.hpp"
#include "tp_command.hpp"

namespace arv::base {

void register_server_commands(command::CommandRegistry& registry) {
  commands::register_tp_command(registry);
  commands::register_selection_command(registry);
  commands::register_blueprint_command(registry);
}

} // namespace arv::base
