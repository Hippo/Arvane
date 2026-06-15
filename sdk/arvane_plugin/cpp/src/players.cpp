#include "arv/players.hpp"

#include <algorithm>
#include <cstdint>

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

std::vector<ArvPlayerID> Players::online() const {
  std::vector<ArvPlayerID> result;
  if (api_ == nullptr || api_->players == nullptr ||
      api_->players->online_players == nullptr) {
    return result;
  }
  const std::uint64_t count =
      api_->players->online_players(api_->players_ctx, nullptr, 0U);
  result.resize(count);
  const std::uint64_t written = api_->players->online_players(
      api_->players_ctx,
      result.data(),
      result.size());
  result.resize(std::min<std::uint64_t>(written, result.size()));
  return result;
}

std::string Players::profile_id(ArvPlayerID player) const {
  if (api_ == nullptr || api_->players == nullptr ||
      api_->players->profile_id == nullptr) {
    return {};
  }
  return std::string{view(api_->players->profile_id(api_->players_ctx, player))};
}

std::string Players::display_name(ArvPlayerID player) const {
  if (api_ == nullptr || api_->players == nullptr ||
      api_->players->display_name == nullptr) {
    return {};
  }
  return std::string{view(api_->players->display_name(api_->players_ctx, player))};
}

std::optional<ArvPlayerID> Players::find_by_profile_id(std::string_view profile_id) const {
  if (api_ == nullptr || api_->players == nullptr ||
      api_->players->find_by_profile_id == nullptr) {
    return std::nullopt;
  }
  ArvPlayerID player{};
  if (!api_->players->find_by_profile_id(api_->players_ctx, view(profile_id), &player)) {
    return std::nullopt;
  }
  return player;
}

} // namespace arv
