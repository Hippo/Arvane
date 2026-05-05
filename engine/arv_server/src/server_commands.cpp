#include "arv/server/server_commands.hpp"

#include "arv/commands/tp_command.hpp"
#include "arv/server/command_services.hpp"

namespace arv::server {

std::optional<OnlinePlayerCommandTarget> ServerCommandServices::online_player_by_id(
    PlayerID player_id) const {
  if (sessions == nullptr || world == nullptr) {
    return std::nullopt;
  }

  for (const SessionID session_id : sessions->session_ids()) {
    const ServerSession* session = sessions->find(session_id);
    if (session == nullptr || session->player_id != player_id) {
      continue;
    }

    const player::PlayerState* player_state = world->find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    return OnlinePlayerCommandTarget{
        session->session_id,
        session->player_id,
        session->world_id,
        session->display_name,
        player_state->position,
    };
  }

  return std::nullopt;
}

std::optional<OnlinePlayerCommandTarget> ServerCommandServices::online_player_by_name(
    std::string_view display_name) const {
  if (sessions == nullptr || world == nullptr) {
    return std::nullopt;
  }

  std::optional<OnlinePlayerCommandTarget> match;
  for (const SessionID session_id : sessions->session_ids()) {
    const ServerSession* session = sessions->find(session_id);
    if (session == nullptr || session->display_name != display_name) {
      continue;
    }

    const player::PlayerState* player_state = world->find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    if (match.has_value()) {
      return std::nullopt;
    }

    match = OnlinePlayerCommandTarget{
        session->session_id,
        session->player_id,
        session->world_id,
        session->display_name,
        player_state->position,
    };
  }

  return match;
}

bool ServerCommandServices::teleport_player(PlayerID player_id, Vec3f position) const {
  if (world == nullptr) {
    return false;
  }

  player::PlayerState* player_state = world->find_player(player_id);
  if (player_state == nullptr) {
    return false;
  }

  player_state->position = position;
  player_state->velocity = {};
  player_state->grounded = false;

  if (const std::optional<EntityID> entity_id = world->entity_for_player(player_id);
      entity_id.has_value()) {
    (void)world->set_entity_position(entity_id.value(), position);
  }

  return true;
}

void register_server_commands(command::CommandRegistry& registry) {
  commands::register_tp_command(registry);
}

command::CommandSender server_console_command_sender() {
  command::CommandSender sender;
  sender.kind = command::CommandSourceKind::ServerConsole;
  sender.display_name = "server_console";
  return sender;
}

} // namespace arv::server
