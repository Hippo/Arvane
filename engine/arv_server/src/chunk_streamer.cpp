#include "arv/server/chunk_streamer.hpp"

#include "arv/core/profiling.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <optional>
#include <utility>

namespace arv::server {
namespace {

inline constexpr u32 ReadySpreadFramesAfterTurn = 24U;

struct ColumnGenerationPriority {
  i32 distance{};
  i32 forward{};
  i32 lateral{};
  i32 z{};
  i32 x{};
};

[[nodiscard]] bool operator<(
    ColumnGenerationPriority lhs,
    ColumnGenerationPriority rhs) noexcept {
  if (lhs.distance != rhs.distance) {
    return lhs.distance < rhs.distance;
  }
  if (lhs.forward != rhs.forward) {
    return lhs.forward < rhs.forward;
  }
  if (lhs.lateral != rhs.lateral) {
    return lhs.lateral < rhs.lateral;
  }
  if (lhs.z != rhs.z) {
    return lhs.z < rhs.z;
  }
  return lhs.x < rhs.x;
}

template<typename PlayerState>
[[nodiscard]] ColumnGenerationPriority generation_priority(
    const PlayerState& state,
    ChunkCoord column) noexcept {
  const i32 dx = column.x - state.center_chunk.value.x;
  const i32 dz = column.z - state.center_chunk.value.z;
  const ChunkSectionCoord priority_coord{{column.x, state.center_chunk.value.y, column.z}};
  ColumnGenerationPriority priority;
  priority.distance = chunk_horizontal_chebyshev_distance(priority_coord, state.center_chunk);
  priority.forward = 0;
  priority.lateral = 0;
  if (state.movement_x != 0 || state.movement_z != 0) {
    priority.forward = -(dx * state.movement_x + dz * state.movement_z);
    priority.lateral = std::abs(dx * state.movement_z - dz * state.movement_x);
  }
  priority.z = column.z;
  priority.x = column.x;
  return priority;
}

template<typename PlayerState>
[[nodiscard]] bool column_has_requested_section(
    const PlayerState& state,
    const world::WorldRuntime& world,
    ChunkCoord column,
    world::WorldHeightSettings height) {
  for (i32 y = height.min_chunk_y; y <= height.max_chunk_y; ++y) {
    const ChunkSectionCoord coord{{column.x, y, column.z}};
    if (state.desired_chunks.find(coord) == state.desired_chunks.end() ||
        state.pending_chunks.find(coord) == state.pending_chunks.end()) {
      continue;
    }
    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk != nullptr &&
        chunk->lifecycle_state() == voxel::ChunkLifecycleState::Requested) {
      return true;
    }
  }
  return false;
}

template<typename PlayerState>
void rebuild_requested_generation_order(PlayerState& state) {
  std::vector<ChunkCoord> ordered;
  ordered.reserve(state.requested_generation_columns.size());
  HashSet<ChunkCoord> added;
  added.reserve(state.requested_generation_columns.size());

  for (const ChunkCoord column : state.scan_columns) {
    if (state.requested_generation_columns.find(column) ==
        state.requested_generation_columns.end()) {
      continue;
    }
    ordered.emplace_back(column);
    added.insert(column);
  }
  for (const ChunkCoord column : state.requested_generation_order) {
    if (state.requested_generation_columns.find(column) ==
            state.requested_generation_columns.end() ||
        added.find(column) != added.end()) {
      continue;
    }
    ordered.emplace_back(column);
    added.insert(column);
  }
  for (const ChunkCoord column : state.requested_generation_columns) {
    if (added.find(column) != added.end()) {
      continue;
    }
    ordered.emplace_back(column);
  }
  state.requested_generation_order = std::move(ordered);
}

[[nodiscard]] std::vector<ChunkCoord> chunk_columns_by_distance(
    ChunkSectionCoord center,
    i32 radius,
    i32 movement_x,
    i32 movement_z) {
  std::vector<ChunkCoord> columns;
  const i32 side = radius * 2 + 1;
  columns.reserve(static_cast<usize>(side * side));
  for (i32 distance = 0; distance <= radius; ++distance) {
    std::vector<ChunkCoord> ring;
    const i32 min_z = center.value.z - distance;
    const i32 max_z = center.value.z + distance;
    const i32 min_x = center.value.x - distance;
    const i32 max_x = center.value.x + distance;
    for (i32 z = min_z; z <= max_z; ++z) {
      for (i32 x = min_x; x <= max_x; ++x) {
        const ChunkSectionCoord coord{{x, center.value.y, z}};
        if (chunk_horizontal_chebyshev_distance(coord, center) != distance) {
          continue;
        }
        ring.push_back({x, z});
      }
    }
    std::sort(
        ring.begin(),
        ring.end(),
        [center, movement_x, movement_z](ChunkCoord lhs, ChunkCoord rhs) {
          if (movement_x != 0 || movement_z != 0) {
            const i32 lhs_dx = lhs.x - center.value.x;
            const i32 lhs_dz = lhs.z - center.value.z;
            const i32 rhs_dx = rhs.x - center.value.x;
            const i32 rhs_dz = rhs.z - center.value.z;
            const i32 lhs_forward = lhs_dx * movement_x + lhs_dz * movement_z;
            const i32 rhs_forward = rhs_dx * movement_x + rhs_dz * movement_z;
            if (lhs_forward != rhs_forward) {
              return lhs_forward > rhs_forward;
            }
            const i32 lhs_lateral = std::abs(lhs_dx * movement_z - lhs_dz * movement_x);
            const i32 rhs_lateral = std::abs(rhs_dx * movement_z - rhs_dz * movement_x);
            if (lhs_lateral != rhs_lateral) {
              return lhs_lateral < rhs_lateral;
            }
          }
          if (lhs.z != rhs.z) {
            return lhs.z < rhs.z;
          }
          return lhs.x < rhs.x;
        });
    for (const ChunkCoord column : ring) {
      columns.emplace_back(column);
    }
  }
  return columns;
}

template<typename Fn>
void visit_chunk_columns_by_distance(
    ChunkSectionCoord center,
    i32 radius,
    i32 movement_x,
    i32 movement_z,
    Fn&& fn) {
  const std::vector<ChunkCoord> columns =
      chunk_columns_by_distance(center, radius, movement_x, movement_z);
  for (const ChunkCoord column : columns) {
    const ChunkSectionCoord coord{{column.x, center.value.y, column.z}};
    const i32 distance = chunk_horizontal_chebyshev_distance(coord, center);
    if (!fn(column, distance)) {
      return;
    }
  }
}

template<typename Fn>
void visit_chunk_columns_by_distance(ChunkSectionCoord center, i32 radius, Fn&& fn) {
  visit_chunk_columns_by_distance(center, radius, 0, 0, std::forward<Fn>(fn));
}

[[nodiscard]] std::vector<i32> vertical_priority_order(
    world::WorldHeightSettings height,
    i32 center_y) {
  std::vector<i32> result;
  result.reserve(height.section_count());
  for (i32 distance = 0; result.size() < height.section_count(); ++distance) {
    bool added = false;
    const i32 lower = center_y - distance;
    const i32 upper = center_y + distance;
    if (lower >= height.min_chunk_y && lower <= height.max_chunk_y) {
      result.push_back(lower);
      added = true;
    }
    if (distance != 0 && upper >= height.min_chunk_y && upper <= height.max_chunk_y) {
      result.push_back(upper);
      added = true;
    }
    if (!added && lower < height.min_chunk_y && upper > height.max_chunk_y) {
      break;
    }
  }
  return result;
}

template<typename PlayerState>
[[nodiscard]] std::optional<ChunkSectionCoord> next_pending_chunk_for_send(
    const PlayerState& state,
    world::WorldHeightSettings height,
    const HashSet<ChunkSectionCoord>& deferred_this_tick) {
  const std::vector<i32> y_order = vertical_priority_order(height, state.center_chunk.value.y);
  std::optional<ChunkSectionCoord> result;
  const std::vector<ChunkCoord> fallback_columns =
      state.scan_columns.empty()
          ? chunk_columns_by_distance(
                state.center_chunk,
                state.scan_distance,
                state.movement_x,
                state.movement_z)
          : std::vector<ChunkCoord>{};
  const std::vector<ChunkCoord>& columns =
      state.scan_columns.empty() ? fallback_columns : state.scan_columns;
  for (const i32 y : y_order) {
    for (const ChunkCoord column : columns) {
      const ChunkSectionCoord coord{{column.x, y, column.z}};
      if (deferred_this_tick.find(coord) != deferred_this_tick.end() ||
          state.pending_chunks.find(coord) == state.pending_chunks.end() ||
          state.desired_chunks.find(coord) == state.desired_chunks.end()) {
        continue;
      }
      result = coord;
      return result;
    }
  }
  return result;
}

} // namespace

void ServerChunkStreamer::configure(ServerChunkStreamingConfig config) noexcept {
  if (config.max_view_distance < 0) {
    config.max_view_distance = 0;
  }
  if (config.simulation_distance < 0) {
    config.simulation_distance = 0;
  }
  config.simulation_distance = std::min(config.simulation_distance, config.max_view_distance);
  if (config.unload_margin < 0) {
    config.unload_margin = 0;
  }
  if (config.forward_prefetch_chunks < 0) {
    config.forward_prefetch_chunks = 0;
  }
  if (config.max_chunks_sent_per_player_tick == 0U) {
    config.max_chunks_sent_per_player_tick = 1U;
  }
  if (config.max_unloads_sent_per_player_tick == 0U) {
    config.max_unloads_sent_per_player_tick = 1U;
  }
  if (config.max_ready_chunks_sent_per_player_frame == 0U) {
    config.max_ready_chunks_sent_per_player_frame =
        config.max_chunks_sent_per_player_tick;
  }
  if (config.max_missing_columns_requested_per_player_tick == 0U) {
    config.max_missing_columns_requested_per_player_tick =
        config.max_chunks_sent_per_player_tick;
  }
  if (config.max_generation_results_per_frame == 0U) {
    config.max_generation_results_per_frame = 1U;
  }
  if (config.max_generation_schedules_per_frame == 0U) {
    config.max_generation_schedules_per_frame = 1U;
  }
  config_ = config;
}

void ServerChunkStreamer::update_player_interest(
    const ServerSession& session,
    ChunkSectionCoord authoritative_center,
    world::WorldHeightSettings height,
    const protocol::ServerboundChunkInterestUpdate& interest) {
  ARV_PROFILE_ZONE("Chunk demand discovery");
  PlayerChunkStreamState& state = players_[session.session_id];
  const bool new_state = state.session_id != session.session_id;
  if (new_state) {
    state.session_id = session.session_id;
    state.player_id = session.player_id;
  }

  state.connection_id = session.connection_id;
  const WorldID authoritative_world = session.world_id;
  const i32 authoritative_distance = clamp_view_distance(interest.client_view_distance);
  const i32 previous_movement_x = state.movement_x;
  const i32 previous_movement_z = state.movement_z;
  const i32 previous_scan_distance = state.scan_distance;
  const std::optional<ChunkSectionCoord> previous_center =
      interest.previous_center_chunk.has_value()
          ? interest.previous_center_chunk
          : (!new_state && state.world_id == authoritative_world
                 ? std::optional<ChunkSectionCoord>{state.center_chunk}
                 : std::nullopt);

  if (!new_state && state.world_id != authoritative_world) {
    for (const auto& [coord, version] : state.sent_chunk_versions) {
      (void)version;
      state.unload_queue.emplace_back(coord);
    }
    state.sent_chunk_versions.clear();
    state.pending_chunks.clear();
    state.requested_generation_columns.clear();
    state.requested_generation_order.clear();
  }

  state.world_id = authoritative_world;
  state.center_chunk = authoritative_center;
  i32 center_delta_x = 0;
  i32 center_delta_z = 0;
  if (previous_center.has_value()) {
    center_delta_x = authoritative_center.value.x - previous_center->value.x;
    center_delta_z = authoritative_center.value.z - previous_center->value.z;
    if (center_delta_x != 0 || center_delta_z != 0) {
      state.movement_x = center_delta_x == 0 ? 0 : (center_delta_x > 0 ? 1 : -1);
      state.movement_z = center_delta_z == 0 ? 0 : (center_delta_z > 0 ? 1 : -1);
    }
  } else {
    state.movement_x = 0;
    state.movement_z = 0;
  }
  if (!new_state &&
      (state.movement_x != previous_movement_x ||
       state.movement_z != previous_movement_z) &&
      (state.movement_x != 0 || state.movement_z != 0)) {
    state.ready_spread_frames =
        std::max(state.ready_spread_frames, ReadySpreadFramesAfterTurn);
  }
  state.view_distance = authoritative_distance;
  state.scan_distance = authoritative_distance;
  HashSet<ChunkSectionCoord> desired =
      compute_streaming_set(authoritative_center, height, authoritative_distance);
  if ((state.movement_x != 0 || state.movement_z != 0) &&
      config_.forward_prefetch_chunks > 0 &&
      authoritative_distance > 0) {
    const i32 prefetch_distance = config_.forward_prefetch_chunks;
    const i32 prefetch_radius =
        std::min<i32>(2, authoritative_distance);
    const ChunkSectionCoord prefetch_center{{
        authoritative_center.value.x + state.movement_x * prefetch_distance,
        authoritative_center.value.y,
        authoritative_center.value.z + state.movement_z * prefetch_distance}};
    HashSet<ChunkSectionCoord> prefetch =
        compute_streaming_set(prefetch_center, height, prefetch_radius);
    desired.insert(prefetch.begin(), prefetch.end());
    state.scan_distance = authoritative_distance + prefetch_distance;
  }
  const bool can_translate_scan_columns =
      previous_center.has_value() &&
      !state.scan_columns.empty() &&
      previous_scan_distance == state.scan_distance &&
      previous_movement_x == state.movement_x &&
      previous_movement_z == state.movement_z;
  if (can_translate_scan_columns) {
    for (ChunkCoord& column : state.scan_columns) {
      column.x += center_delta_x;
      column.z += center_delta_z;
    }
  } else {
    state.scan_columns = chunk_columns_by_distance(
        state.center_chunk,
        state.scan_distance,
        state.movement_x,
        state.movement_z);
  }
  compute_streaming_diff(
      state,
      height,
      std::move(desired));
  rebuild_requested_generation_order(state);
}

void ServerChunkStreamer::remove_session(SessionID session_id) {
  players_.erase(session_id);
}

void ServerChunkStreamer::tick_streaming_budget(
    world::WorldRuntime& world,
    std::vector<QueuedChunkStreamMessage>& out_messages) {
  for (auto& [session_id, state] : players_) {
    (void)session_id;
    if (state.world_id != world.id()) {
      continue;
    }

    queue_dirty_sent_chunks(state, world);
    queue_chunk_unloads(state, out_messages);
    queue_chunk_sends(
        world,
        state,
        out_messages,
        config_.max_chunks_sent_per_player_tick,
        true);
  }
}

void ServerChunkStreamer::tick_ready_chunk_budget(
    world::WorldRuntime& world,
    std::vector<QueuedChunkStreamMessage>& out_messages,
    u32 max_chunks_sent_per_player) {
  if (max_chunks_sent_per_player == 0U) {
    return;
  }

  for (auto& [session_id, state] : players_) {
    (void)session_id;
    if (state.world_id != world.id()) {
      continue;
    }

    queue_chunk_sends(
        world,
        state,
        out_messages,
        max_chunks_sent_per_player,
        false);
  }
}

void ServerChunkStreamer::tick_streaming_maintenance(
    world::WorldRuntime& world,
    std::vector<QueuedChunkStreamMessage>& out_messages,
    u32 max_missing_columns_per_player) {
  for (auto& [session_id, state] : players_) {
    (void)session_id;
    if (state.world_id != world.id()) {
      continue;
    }

    queue_dirty_sent_chunks(state, world);
    queue_chunk_unloads(state, out_messages);
    queue_missing_chunk_requests(
        world,
        state,
        max_missing_columns_per_player);
  }
}

void ServerChunkStreamer::tick_missing_chunk_requests(
    world::WorldRuntime& world,
    u32 max_missing_columns_per_player) {
  if (max_missing_columns_per_player == 0U) {
    return;
  }

  for (auto& [session_id, state] : players_) {
    (void)session_id;
    if (state.world_id != world.id()) {
      continue;
    }

    queue_missing_chunk_requests(
        world,
        state,
        max_missing_columns_per_player);
  }
}

std::vector<ChunkCoord> ServerChunkStreamer::pending_generation_requests(
    const world::WorldRuntime& world,
    usize max_requests,
    bool queued_only) {
  ARV_PROFILE_ZONE("Chunk priority selection");
  std::vector<ChunkCoord> requests;
  if (max_requests != 0U) {
    requests.reserve(max_requests);
  }
  HashMap<ChunkCoord, ColumnGenerationPriority> request_priorities;
  const world::WorldHeightSettings height = world.height_settings();
  const bool bounded = max_requests != 0U;
  const bool single_player_fast_path = bounded && players_.size() <= 1U;
  u32 matching_players = 0;
  for (auto& [session_id, state] : players_) {
    (void)session_id;
    if (state.world_id != world.id()) {
      continue;
    }
    ++matching_players;
    if (bounded && requests.size() >= max_requests) {
      break;
    }
    if (bounded && !state.requested_generation_columns.empty()) {
      if (state.requested_generation_order.empty()) {
        rebuild_requested_generation_order(state);
      }
      const auto add_requested_column = [&](ChunkCoord column, bool validate_requested_sections) {
        if (requests.size() >= max_requests) {
          return false;
        }
        if (!validate_requested_sections ||
            column_has_requested_section(state, world, column, height)) {
          if (single_player_fast_path) {
            requests.emplace_back(column);
            return true;
          }
          const ColumnGenerationPriority priority = generation_priority(state, column);
          auto [priority_iter, inserted] = request_priorities.try_emplace(column, priority);
          if (inserted) {
            requests.emplace_back(column);
          } else {
            if (priority < priority_iter->second) {
              priority_iter->second = priority;
            }
          }
        }
        return true;
      };

      for (usize order_index = 0;
           order_index < state.requested_generation_order.size();) {
        const ChunkCoord column = state.requested_generation_order[order_index];
        const bool still_requested =
            state.requested_generation_columns.find(column) !=
            state.requested_generation_columns.end();
        if (!still_requested ||
            !column_has_requested_section(state, world, column, height)) {
          state.requested_generation_columns.erase(column);
          state.requested_generation_order.erase(
              state.requested_generation_order.begin() +
              static_cast<isize>(order_index));
          continue;
        }
        if (!add_requested_column(column, false)) {
          break;
        }
        ++order_index;
      }

      if (requests.size() < max_requests) {
        std::vector<ChunkCoord> candidates;
        candidates.reserve(state.requested_generation_columns.size());
        for (const ChunkCoord column : state.requested_generation_columns) {
          if (state.scan_columns.empty() ||
              std::find(state.scan_columns.begin(), state.scan_columns.end(), column) ==
                  state.scan_columns.end()) {
            if (column_has_requested_section(state, world, column, height)) {
              candidates.emplace_back(column);
            }
          }
        }
        std::sort(
            candidates.begin(),
            candidates.end(),
            [&state](ChunkCoord lhs, ChunkCoord rhs) {
              return generation_priority(state, lhs) < generation_priority(state, rhs);
            });
        for (const ChunkCoord column : candidates) {
          if (!add_requested_column(column, false)) {
            break;
          }
        }
      }
      if (requests.size() >= max_requests) {
        continue;
      }
    }
    if (bounded && queued_only) {
      continue;
    }
    if (bounded) {
      const auto consider_column = [&](ChunkCoord column) {
        if (requests.size() >= max_requests) {
          return false;
        }
        if (!column_has_requested_section(state, world, column, height)) {
          return true;
        }
        if (single_player_fast_path) {
          requests.emplace_back(column);
          return true;
        }
        const ColumnGenerationPriority priority = generation_priority(state, column);
        auto [priority_iter, inserted] =
            request_priorities.try_emplace(column, priority);
        if (inserted) {
          requests.emplace_back(column);
        } else {
          if (priority < priority_iter->second) {
            priority_iter->second = priority;
          }
        }
        return true;
      };
      if (!state.scan_columns.empty()) {
        for (const ChunkCoord column : state.scan_columns) {
          if (!consider_column(column)) {
            break;
          }
        }
      } else {
        visit_chunk_columns_by_distance(
            state.center_chunk,
            state.scan_distance,
            state.movement_x,
            state.movement_z,
            [&](ChunkCoord column, i32) {
              return consider_column(column);
            });
      }
      continue;
    }
    for (const ChunkSectionCoord coord : state.pending_chunks) {
      if (state.desired_chunks.find(coord) == state.desired_chunks.end()) {
        continue;
      }
      const voxel::ChunkSection* chunk = world.find_chunk(coord);
      if (chunk == nullptr ||
          chunk->lifecycle_state() != voxel::ChunkLifecycleState::Requested) {
        continue;
      }
      const ChunkCoord column = chunk_to_column(coord);
      const ColumnGenerationPriority priority = generation_priority(state, column);
      auto [priority_iter, inserted] = request_priorities.try_emplace(column, priority);
      if (inserted) {
        requests.emplace_back(column);
      } else {
        if (priority < priority_iter->second) {
          priority_iter->second = priority;
        }
      }
    }
  }

  if (bounded && matching_players <= 1U) {
    return requests;
  }

  std::sort(
      requests.begin(),
      requests.end(),
      [&request_priorities](ChunkCoord lhs, ChunkCoord rhs) {
        return request_priorities.at(lhs) < request_priorities.at(rhs);
      });
  return requests;
}

void ServerChunkStreamer::mark_generation_request_scheduled(
    WorldID world_id,
    ChunkCoord column) noexcept {
  for (auto& [session_id, state] : players_) {
    (void)session_id;
    if (state.world_id == world_id) {
      state.requested_generation_columns.erase(column);
      auto& order = state.requested_generation_order;
      order.erase(
          std::remove(order.begin(), order.end(), column),
          order.end());
    }
  }
}

bool ServerChunkStreamer::session_has_sent_chunk(
    SessionID session_id,
    WorldID world_id,
    ChunkSectionCoord coord) const noexcept {
  const auto found = players_.find(session_id);
  return found != players_.end() &&
         found->second.world_id == world_id &&
         found->second.sent_chunk_versions.find(coord) != found->second.sent_chunk_versions.end();
}

bool ServerChunkStreamer::mark_sent_block_version(
    SessionID session_id,
    WorldID world_id,
    ChunkSectionCoord coord,
    u64 block_version) noexcept {
  auto player = players_.find(session_id);
  if (player == players_.end() || player->second.world_id != world_id) {
    return false;
  }
  auto sent = player->second.sent_chunk_versions.find(coord);
  if (sent == player->second.sent_chunk_versions.end()) {
    return false;
  }
  sent->second.block_version = block_version;
  sent->second.lighting_valid_sent = false;
  return true;
}

bool ServerChunkStreamer::confirm_chunk_sent(
    SessionID session_id,
    WorldID world_id,
    ChunkSectionCoord coord,
    u64 block_version,
    u64 light_version,
    bool lighting_valid) noexcept {
  auto player = players_.find(session_id);
  if (player == players_.end() || player->second.world_id != world_id) {
    return false;
  }

  PlayerChunkStreamState& state = player->second;
  if (state.desired_chunks.find(coord) == state.desired_chunks.end()) {
    return false;
  }

  state.sent_chunk_versions[coord] = {
      block_version,
      light_version,
      lighting_valid};
  state.pending_chunks.erase(coord);
  return true;
}

i32 ServerChunkStreamer::clamp_view_distance(i32 requested_distance) const noexcept {
  if (requested_distance < 0) {
    return 0;
  }
  return std::min(requested_distance, config_.max_view_distance);
}

HashSet<ChunkSectionCoord> ServerChunkStreamer::compute_streaming_set(
    ChunkSectionCoord center,
    world::WorldHeightSettings height,
    i32 radius) const {
  HashSet<ChunkSectionCoord> chunks;
  const i32 side = radius * 2 + 1;
  chunks.reserve(static_cast<usize>(side * side) * height.section_count());
  for (i32 z = center.value.z - radius; z <= center.value.z + radius; ++z) {
    for (i32 x = center.value.x - radius; x <= center.value.x + radius; ++x) {
      for (i32 y = height.min_chunk_y; y <= height.max_chunk_y; ++y) {
        chunks.insert({{x, y, z}});
      }
    }
  }
  return chunks;
}

void ServerChunkStreamer::compute_streaming_diff(
    PlayerChunkStreamState& state,
    world::WorldHeightSettings height,
    HashSet<ChunkSectionCoord> next_desired) {
  ARV_PROFILE_ZONE("Chunk streaming diff");
  const HashSet<ChunkSectionCoord> retained_chunks = compute_streaming_set(
      state.center_chunk,
      height,
      state.view_distance + config_.unload_margin);

  for (const auto& [coord, version] : state.sent_chunk_versions) {
    (void)version;
    if (retained_chunks.find(coord) == retained_chunks.end()) {
      state.unload_queue.emplace_back(coord);
    }
  }

  for (auto iter = state.sent_chunk_versions.begin();
       iter != state.sent_chunk_versions.end();) {
    if (retained_chunks.find(iter->first) == retained_chunks.end()) {
      iter = state.sent_chunk_versions.erase(iter);
    } else {
      ++iter;
    }
  }

  for (auto iter = state.pending_chunks.begin(); iter != state.pending_chunks.end();) {
    if (next_desired.find(*iter) == next_desired.end()) {
      iter = state.pending_chunks.erase(iter);
    } else {
      ++iter;
    }
  }

  for (const ChunkSectionCoord coord : next_desired) {
    if (state.sent_chunk_versions.find(coord) == state.sent_chunk_versions.end()) {
      state.pending_chunks.insert(coord);
    }
  }

  for (auto iter = state.requested_generation_columns.begin();
       iter != state.requested_generation_columns.end();) {
    bool still_pending = false;
    for (i32 y = height.min_chunk_y; y <= height.max_chunk_y; ++y) {
      if (state.pending_chunks.find(chunk_section(*iter, y)) != state.pending_chunks.end()) {
        still_pending = true;
        break;
      }
    }
    if (still_pending) {
      ++iter;
    } else {
      iter = state.requested_generation_columns.erase(iter);
    }
  }
  state.desired_chunks = std::move(next_desired);
}

void ServerChunkStreamer::queue_dirty_sent_chunks(
    PlayerChunkStreamState& state,
    const world::WorldRuntime& world) {
  ARV_PROFILE_ZONE("Chunk dirty resend scan");
  // TODO(arvane): Replace full dirty chunk replacement with ChunkDelta /
  // ChunkBlockUpdate once the exact delta payload schema is specified.
  for (const auto& [coord, sent_version] : state.sent_chunk_versions) {
    if (state.desired_chunks.find(coord) == state.desired_chunks.end()) {
      continue;
    }

    const std::optional<voxel::ChunkSnapshot> snapshot = world.get_chunk_snapshot(coord);
    if (!snapshot.has_value()) {
      continue;
    }
    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
      continue;
    }
    const bool lighting_current = !chunk->is_dirty(voxel::ChunkDirtyFlags::Lighting);
    if (snapshot->source_version != sent_version.block_version ||
        chunk->light_data_version() != sent_version.light_version ||
        (lighting_current && !sent_version.lighting_valid_sent)) {
      state.pending_chunks.insert(coord);
    }
  }
}

std::optional<voxel::ChunkSnapshot> ServerChunkStreamer::prepare_chunk_snapshot(
    world::WorldRuntime& world,
    ChunkSectionCoord coord) const {
  ARV_PROFILE_ZONE("Chunk stream snapshot lookup");
  if (std::optional<voxel::ChunkSnapshot> snapshot = world.get_chunk_snapshot(coord);
      snapshot.has_value()) {
    return snapshot;
  }

  world.request_chunk_load(coord);
  return std::nullopt;
}

void ServerChunkStreamer::queue_chunk_unloads(
    PlayerChunkStreamState& state,
    std::vector<QueuedChunkStreamMessage>& out_messages) {
  ARV_PROFILE_ZONE("Chunk unload queue");
  u32 sent = 0;
  while (sent < config_.max_unloads_sent_per_player_tick && !state.unload_queue.empty()) {
    const ChunkSectionCoord coord = state.unload_queue.back();
    state.unload_queue.pop_back();

    protocol::ClientboundChunkSectionUnload unload;
    unload.world_id = state.world_id;
    unload.coord = coord;
    QueuedChunkStreamMessage message;
    message.session_id = state.session_id;
    message.connection_id = state.connection_id;
    message.world_id = state.world_id;
    message.reliability = protocol::ReliabilityClass::ReliableOrdered;
    message.lane = protocol::NetworkLane::ChunkStreaming;
    message.message_type = protocol::MessageType::ClientboundChunkSectionUnload;
    message.payload = protocol::serialize_clientbound_chunk_section_unload(unload);
    message.coord = coord;
    out_messages.emplace_back(std::move(message));
    ++sent;
  }
}

void ServerChunkStreamer::queue_missing_chunk_requests(
    world::WorldRuntime& world,
    PlayerChunkStreamState& state,
    u32 max_missing_columns) {
  ARV_PROFILE_ZONE("Chunk missing request scan");
  u32 requested_columns = 0;
  const world::WorldHeightSettings height = world.height_settings();
  const std::vector<i32> y_order = vertical_priority_order(
      height,
      state.center_chunk.value.y);
  const auto section_ready_or_requested = [&](ChunkSectionCoord coord) {
    const voxel::ChunkSection* chunk = world.find_chunk(coord);
    if (chunk == nullptr) {
      world.request_chunk_load(coord);
      return false;
    }
    switch (chunk->lifecycle_state()) {
      case voxel::ChunkLifecycleState::Loaded:
      case voxel::ChunkLifecycleState::Active:
      case voxel::ChunkLifecycleState::Loading:
      case voxel::ChunkLifecycleState::Generating:
      case voxel::ChunkLifecycleState::Populating:
        return true;
      case voxel::ChunkLifecycleState::Unloaded:
      case voxel::ChunkLifecycleState::Requested:
      case voxel::ChunkLifecycleState::MarkedForUnload:
      case voxel::ChunkLifecycleState::Saving:
        return false;
    }
    return false;
  };
  const auto consider_column = [&](ChunkCoord column) {
    if (requested_columns >= max_missing_columns) {
      return false;
    }
    if (state.requested_generation_columns.find(column) !=
        state.requested_generation_columns.end()) {
      return true;
    }
    for (const i32 y : y_order) {
      const ChunkSectionCoord coord{{column.x, y, column.z}};
      if (state.pending_chunks.find(coord) == state.pending_chunks.end() ||
          state.desired_chunks.find(coord) == state.desired_chunks.end()) {
        continue;
      }
      if (!section_ready_or_requested(coord)) {
        const auto [unused, inserted] =
            state.requested_generation_columns.insert(column);
        (void)unused;
        if (inserted) {
          state.requested_generation_order.emplace_back(column);
        }
        ++requested_columns;
        return true;
      }
    }
    return true;
  };

  if (!state.scan_columns.empty()) {
    for (const ChunkCoord column : state.scan_columns) {
      if (!consider_column(column)) {
        return;
      }
    }
    return;
  }

  visit_chunk_columns_by_distance(
      state.center_chunk,
      state.scan_distance,
      state.movement_x,
      state.movement_z,
      [&](ChunkCoord column, i32) {
        return consider_column(column);
      });
}

void ServerChunkStreamer::queue_chunk_sends(
    world::WorldRuntime& world,
    PlayerChunkStreamState& state,
    std::vector<QueuedChunkStreamMessage>& out_messages,
    u32 max_chunks_sent,
    bool request_missing_chunks) {
  ARV_PROFILE_ZONE("Chunk stream send budget");
  u32 sent = 0;
  u32 inspected = 0;
  const u32 scan_multiplier = request_missing_chunks ? 2U : 16U;
  const u32 request_scan_budget =
      max_chunks_sent <= std::numeric_limits<u32>::max() / scan_multiplier
          ? max_chunks_sent * scan_multiplier
          : max_chunks_sent;
  HashSet<ChunkSectionCoord> deferred_this_tick;
  const world::WorldHeightSettings height = world.height_settings();
  const std::vector<i32> y_order = vertical_priority_order(height, state.center_chunk.value.y);
  const std::vector<ChunkCoord> fallback_columns =
      state.scan_columns.empty()
          ? chunk_columns_by_distance(
                state.center_chunk,
                state.scan_distance,
                state.movement_x,
                state.movement_z)
          : std::vector<ChunkCoord>{};
  const std::vector<ChunkCoord>& columns =
      state.scan_columns.empty() ? fallback_columns : state.scan_columns;

  const auto defer_column = [&](ChunkCoord column) {
    for (i32 y = height.min_chunk_y; y <= height.max_chunk_y; ++y) {
      deferred_this_tick.insert({{column.x, y, column.z}});
    }
  };

  const auto try_send_coord = [&](ChunkSectionCoord coord) {
    ++inspected;
    if (state.desired_chunks.find(coord) == state.desired_chunks.end()) {
      state.pending_chunks.erase(coord);
      return;
    }

    const std::optional<voxel::ChunkSnapshot> snapshot =
        request_missing_chunks ? prepare_chunk_snapshot(world, coord)
                               : world.get_chunk_snapshot(coord);
    if (!snapshot.has_value()) {
      const ChunkCoord column = chunk_to_column(coord);
      defer_column(column);
      if (request_missing_chunks) {
        const auto [unused, inserted] =
            state.requested_generation_columns.insert(column);
        (void)unused;
        if (inserted) {
          state.requested_generation_order.emplace_back(column);
        }
      }
      return;
    }

    const voxel::ChunkSection* source_chunk = world.find_chunk(coord);
    if (source_chunk == nullptr) {
      state.pending_chunks.erase(coord);
      return;
    }

    const u64 chunk_light_version = source_chunk->light_data_version();
    const bool lighting_valid = !source_chunk->is_dirty(voxel::ChunkDirtyFlags::Lighting);
    const bool has_lighting = lighting_valid;
    const bool has_fluids = source_chunk->has_fluids();

    QueuedChunkStreamMessage message;
    message.session_id = state.session_id;
    message.connection_id = state.connection_id;
    message.world_id = state.world_id;
    message.reliability = protocol::ReliabilityClass::ReliableOrdered;
    message.lane = protocol::NetworkLane::ChunkStreaming;
    message.message_type = protocol::MessageType::ClientboundChunkSectionData;
    message.coord = coord;
    message.block_version = snapshot->source_version;
    message.light_version = chunk_light_version;
    message.lighting_valid = lighting_valid;
    message.tracks_sent_chunk = true;
    {
      ARV_PROFILE_ZONE("Chunk stream serialization");
      message.payload = protocol::serialize_clientbound_chunk_section_data_arrays(
          state.world_id,
          coord,
          snapshot->source_version,
          snapshot->source_content_version,
          chunk_light_version,
          has_lighting,
          lighting_valid,
          has_fluids,
          snapshot->blocks(),
          has_lighting ? &snapshot->lights() : nullptr,
          has_fluids ? &snapshot->fluids() : nullptr);
    }
    out_messages.emplace_back(std::move(message));
    deferred_this_tick.insert(coord);
    ++sent;
  };

  const bool spread_ready_chunks =
      !request_missing_chunks;
  if (spread_ready_chunks && state.ready_spread_frames > 0U) {
    --state.ready_spread_frames;
  }

  const auto visit_coord = [&](ChunkSectionCoord coord) {
    if (sent >= max_chunks_sent || inspected >= request_scan_budget) {
      return false;
    }
    if (deferred_this_tick.find(coord) != deferred_this_tick.end() ||
        state.pending_chunks.find(coord) == state.pending_chunks.end() ||
        state.desired_chunks.find(coord) == state.desired_chunks.end()) {
      return true;
    }
    try_send_coord(coord);
    return sent < max_chunks_sent && inspected < request_scan_budget;
  };

  if (spread_ready_chunks) {
    for (const i32 y : y_order) {
      for (const ChunkCoord column : columns) {
        if (!visit_coord({{column.x, y, column.z}})) {
          return;
        }
      }
    }
    return;
  }

  for (const ChunkCoord column : columns) {
    for (const i32 y : y_order) {
      if (!visit_coord({{column.x, y, column.z}})) {
        return;
      }
    }
  }
}

} // namespace arv::server
