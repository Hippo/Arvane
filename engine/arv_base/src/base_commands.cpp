#include "arv/base/base_commands.hpp"

#include "blueprint_command.hpp"
#include "flyspeed_command.hpp"
#include "gamemode_command.hpp"
#include "selection_command.hpp"
#include "spawn_command.hpp"
#include "tp_command.hpp"

namespace arv::base {

void register_server_commands(command::CommandRegistry& registry) {
  commands::register_tp_command(registry);
  commands::register_gamemode_command(registry);
  commands::register_flyspeed_command(registry);
  commands::register_spawn_commands(registry);
  commands::register_selection_command(registry);
  commands::register_blueprint_command(registry);
}

} // namespace arv::base
