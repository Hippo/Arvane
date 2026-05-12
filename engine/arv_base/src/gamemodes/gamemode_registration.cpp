#include "arv/base/base_module.hpp"

#include "arv/base/base_ids.hpp"

#include <cassert>

namespace arv::base {

void register_base_game_modes(player::GameModeRegistry& registry) {
  auto result = registry.add_builtin_mode(std::string{ids::SurvivalGameModeID}, {});
  assert(result && "survival game mode registration failed");
  (void)result;

  result = registry.add_builtin_mode(
      std::string{ids::CreativeGameModeID},
      {
          false,
          true,
          true,
          true,
          true,
          true,
          true,
          true,
      });
  assert(result && "creative game mode registration failed");
  (void)result;

  result = registry.add_builtin_mode(
      std::string{ids::SpectatorGameModeID},
      {
          false,
          false,
          false,
          false,
          false,
          true,
          true,
          false,
      });
  assert(result && "spectator game mode registration failed");
  (void)result;

  result = registry.add_builtin_mode(
      std::string{ids::LobbyGameModeID},
      {
          false,
          false,
          false,
          true,
          false,
          false,
          true,
          true,
      });
  assert(result && "lobby game mode registration failed");
  (void)result;

  result = registry.add_builtin_mode(
      std::string{ids::NoClipGameModeID},
      {
          false,
          false,
          false,
          false,
          false,
          true,
          false,
          false,
      });
  assert(result && "noclip game mode registration failed");
  (void)result;
}

} // namespace arv::base
