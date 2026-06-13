#include "ui_model.hpp"

#include <optional>

namespace arv::desktop {

std::vector<ui::PlayerListEntryView> player_list_for_ui(
    const server::ServerRuntime& server) {
  std::vector<ui::PlayerListEntryView> players;
  players.reserve(server.sessions().size());
  server.sessions().for_each_session([&players](const server::ServerSession& session) {
    players.emplace_back(session.profile_name, std::nullopt);
  });
  return players;
}

} // namespace arv::desktop
