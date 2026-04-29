#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/math.hpp"
#include "arv/protocol/chunk_streaming.hpp"

#include <optional>

namespace arv::client {

class ClientChunkInterest {
public:
  void set_world_id(WorldID world_id) noexcept;
  void set_view_distance(i32 view_distance) noexcept;
  void update_player_position(Vec3f position) noexcept;
  [[nodiscard]] bool consume_interest_changed() noexcept;
  [[nodiscard]] protocol::ChunkInterestUpdate build_interest_update() const noexcept;

  [[nodiscard]] ChunkCoord current_chunk() const noexcept {
    return current_chunk_;
  }

private:
  [[nodiscard]] static ChunkCoord position_to_chunk(Vec3f position) noexcept;

  WorldID world_id_{};
  ChunkCoord current_chunk_{};
  std::optional<ChunkCoord> previous_chunk_{};
  i32 view_distance_{2};
  u32 next_sequence_id_{1};
  bool has_current_chunk_{};
  bool dirty_{true};
};

} // namespace arv::client
