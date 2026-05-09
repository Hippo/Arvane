#include "arv/client/chunk_interest.hpp"

#include <cmath>

namespace arv::client {

void ClientChunkInterest::set_world_id(WorldID world_id) noexcept {
  if (world_id_ == world_id) {
    return;
  }
  world_id_ = world_id;
  previous_chunk_.reset();
  dirty_ = true;
}

void ClientChunkInterest::set_view_distance(i32 view_distance) noexcept {
  if (view_distance < 0) {
    view_distance = 0;
  }
  if (view_distance_ == view_distance) {
    return;
  }
  view_distance_ = view_distance;
  dirty_ = true;
}

void ClientChunkInterest::update_player_position(Vec3f position) noexcept {
  const ChunkSectionCoord next_chunk = position_to_chunk(position);
  if (!has_current_chunk_) {
    current_chunk_ = next_chunk;
    has_current_chunk_ = true;
    dirty_ = true;
    return;
  }

  if (next_chunk == current_chunk_) {
    return;
  }

  previous_chunk_ = current_chunk_;
  current_chunk_ = next_chunk;
  dirty_ = true;
}

bool ClientChunkInterest::consume_interest_changed() noexcept {
  if (!dirty_ || !has_current_chunk_) {
    return false;
  }
  dirty_ = false;
  ++next_sequence_id_;
  return true;
}

protocol::ServerboundChunkInterestUpdate ClientChunkInterest::build_interest_update() const noexcept {
  protocol::ServerboundChunkInterestUpdate update;
  update.world_id = world_id_;
  update.center_chunk = current_chunk_;
  update.client_view_distance = view_distance_;
  update.previous_center_chunk = previous_chunk_;
  update.sequence_id = next_sequence_id_;
  return update;
}

ChunkSectionCoord ClientChunkInterest::position_to_chunk(Vec3f position) noexcept {
  return block_to_chunk({{static_cast<i32>(std::floor(position.x)),
                          static_cast<i32>(std::floor(position.y)),
                          static_cast<i32>(std::floor(position.z))}});
}

} // namespace arv::client
