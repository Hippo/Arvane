#include "world_runtime_internal.hpp"

namespace arv::world::detail {

const RegionRegistry& WorldRegionRuntime::regions() const noexcept {
  return regions_;
}

RegionRegistry& WorldRegionRuntime::regions() noexcept {
  return regions_;
}

const PlayerRegionState* WorldRegionRuntime::player_region_state(PlayerID player_id) const noexcept {
  const auto found = player_region_states_.find(player_id);
  return found == player_region_states_.end() ? nullptr : &found->second;
}

RegionMembershipChange WorldRegionRuntime::update_player_region_state(
    PlayerID player_id,
    RegionQuery query) {
  PlayerRegionState& state = player_region_states_[player_id];
  return regions_.update_membership(state, query);
}

void WorldRegionRuntime::erase_player(PlayerID player_id) noexcept {
  player_region_states_.erase(player_id);
}

} // namespace arv::world::detail

namespace arv::world {

const RegionRegistry& WorldRuntime::regions() const noexcept {
  return impl_->regions.regions();
}

RegionRegistry& WorldRuntime::regions() noexcept {
  assert_owner_thread();
  return impl_->regions.regions();
}

const PlayerRegionState* WorldRuntime::player_region_state(PlayerID player_id) const noexcept {
  return impl_->regions.player_region_state(player_id);
}

RegionMembershipChange WorldRuntime::update_player_region_state(
    PlayerID player_id,
    RegionQuery query) {
  assert_owner_thread();
  return impl_->regions.update_player_region_state(player_id, query);
}

} // namespace arv::world
