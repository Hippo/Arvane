#include "arv/players.hpp"

namespace arv {
namespace {

[[nodiscard]] PlayerSelection from_abi(const ArvPlayerSelection& selection) {
  PlayerSelection out;
  out.has_world = (selection.flags & ARV_PLAYER_SELECTION_HAS_WORLD) != 0U;
  if (out.has_world) {
    out.world = selection.world;
  }
  if ((selection.flags & ARV_PLAYER_SELECTION_HAS_A) != 0U) {
    out.a = selection.a;
  }
  if ((selection.flags & ARV_PLAYER_SELECTION_HAS_B) != 0U) {
    out.b = selection.b;
  }
  return out;
}

} // namespace

Players::Players(const ArvAPI* api) noexcept : api_(api) {
}

ValueResult<PlayerSelection> Players::current_selection(ArvPlayerID player) const {
  ArvPlayerSelection selection{};
  if (api_ == nullptr || api_->players == nullptr ||
      api_->players->current_selection == nullptr) {
    return {Result::unsupported("players API is unavailable"), {}};
  }
  return {
      Result{api_->players->current_selection(api_->players_ctx, player, &selection)},
      from_abi(selection),
  };
}

} // namespace arv
