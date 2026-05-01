#include "arv/server/chunk_streamer.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace arv::server {
namespace {

[[nodiscard]] i32 abs_i32(i32 value) noexcept {
  return value < 0 ? -value : value;
}

[[nodiscard]] i32 horizontal_chunk_distance(ChunkCoord lhs, ChunkCoord rhs) noexcept {
  return std::max(abs_i32(lhs.value.x - rhs.value.x), abs_i32(lhs.value.z - rhs.value.z));
}

[[nodiscard]] bool chunk_order_less(ChunkCoord lhs, ChunkCoord rhs, ChunkCoord center) noexcept {
  const i32 lhs_horizontal_distance = horizontal_chunk_distance(lhs, center);
  const i32 rhs_horizontal_distance = horizontal_chunk_distance(rhs, center);
  if (lhs_horizontal_distance != rhs_horizontal_distance) {
    return lhs_horizontal_distance < rhs_horizontal_distance;
  }
  const i32 lhs_vertical_distance = abs_i32(lhs.value.y - center.value.y);
  const i32 rhs_vertical_distance = abs_i32(rhs.value.y - center.value.y);
  if (lhs_vertical_distance != rhs_vertical_distance) {
    return lhs_vertical_distance < rhs_vertical_distance;
  }
  if (lhs.value.z != rhs.value.z) {
    return lhs.value.z < rhs.value.z;
  }
  if (lhs.value.x != rhs.value.x) {
    return lhs.value.x < rhs.value.x;
  }
  return lhs.value.y < rhs.value.y;
}

[[nodiscard]] i32 generation_priority_distance(ChunkCoord coord, ChunkCoord center) noexcept {
  return horizontal_chunk_distance(coord, center) * 4 + abs_i32(coord.value.y - center.value.y);
}

[[nodiscard]] bool has_any_fluid(const voxel::ChunkSection& chunk) noexcept {
  for (const voxel::FluidCell fluid : chunk.fluids()) {
    if (!fluid.empty()) {
      return true;
    }
  }
  return false;
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
  if (config.max_chunks_sent_per_player_tick == 0U) {
    config.max_chunks_sent_per_player_tick = 1U;
  }
  if (config.max_unloads_sent_per_player_tick == 0U) {
    config.max_unloads_sent_per_player_tick = 1U;
  }
  config_ = config;
}

void ServerChunkStreamer::update_player_interest(
    const ServerSession& session,
    ChunkCoord authoritative_center,
    const protocol::ChunkInterestUpdate& interest) {
  PlayerChunkStreamState& state = players_[session.session_id];
  const bool new_state = state.session_id != session.session_id;
  if (new_state) {
    state.session_id = session.session_id;
    state.player_id = session.player_id;
  }

  state.connection_id = session.connection_id;
  const WorldID authoritative_world = session.world_id;
  const i32 authoritative_distance = clamp_view_distance(interest.client_view_distance);

  if (!new_state && state.world_id != authoritative_world) {
    for (const auto& [coord, version] : state.sent_chunk_versions) {
      (void)version;
      state.unload_queue.emplace_back(coord);
    }
    state.sent_chunk_versions.clear();
    state.pending_chunks.clear();
  }

  state.world_id = authoritative_world;
  state.center_chunk = authoritative_center;
  state.view_distance = authoritative_distance;
  compute_streaming_diff(
      state,
      compute_streaming_set(authoritative_center, authoritative_distance));
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
    queue_chunk_sends(world, state, out_messages);
  }
}

std::vector<ChunkCoord> ServerChunkStreamer::pending_generation_requests(
    const world::WorldRuntime& world) const {
  std::vector<ChunkCoord> requests;
  HashMap<ChunkCoord, i32> request_priorities;
  for (const auto& [session_id, state] : players_) {
    (void)session_id;
    if (state.world_id != world.id()) {
      continue;
    }
    for (const ChunkCoord coord : state.pending_chunks) {
      if (state.desired_chunks.find(coord) == state.desired_chunks.end()) {
        continue;
      }
      const voxel::ChunkSection* chunk = world.find_chunk(coord);
      if (chunk == nullptr ||
          chunk->lifecycle_state() != voxel::ChunkLifecycleState::Requested) {
        continue;
      }
      const i32 priority = generation_priority_distance(coord, state.center_chunk);
      auto [priority_iter, inserted] = request_priorities.try_emplace(coord, priority);
      if (inserted) {
        requests.emplace_back(coord);
      } else {
        priority_iter->second = std::min(priority_iter->second, priority);
      }
    }
  }

  std::sort(
      requests.begin(),
      requests.end(),
      [&request_priorities](ChunkCoord lhs, ChunkCoord rhs) {
        const i32 lhs_distance = request_priorities.at(lhs);
        const i32 rhs_distance = request_priorities.at(rhs);
        if (lhs_distance != rhs_distance) {
          return lhs_distance < rhs_distance;
        }
        if (lhs.value.z != rhs.value.z) {
          return lhs.value.z < rhs.value.z;
        }
        if (lhs.value.x != rhs.value.x) {
          return lhs.value.x < rhs.value.x;
        }
        return lhs.value.y < rhs.value.y;
      });
  return requests;
}

bool ServerChunkStreamer::session_has_sent_chunk(
    SessionID session_id,
    WorldID world_id,
    ChunkCoord coord) const noexcept {
  const auto found = players_.find(session_id);
  return found != players_.end() &&
         found->second.world_id == world_id &&
         found->second.sent_chunk_versions.find(coord) != found->second.sent_chunk_versions.end();
}

bool ServerChunkStreamer::mark_sent_block_version(
    SessionID session_id,
    WorldID world_id,
    ChunkCoord coord,
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
    ChunkCoord coord,
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

HashSet<ChunkCoord> ServerChunkStreamer::compute_streaming_set(
    ChunkCoord center,
    i32 radius) const {
  HashSet<ChunkCoord> chunks;
  const i32 side = radius * 2 + 1;
  chunks.reserve(static_cast<usize>(side * side * side));
  for (i32 z = center.value.z - radius; z <= center.value.z + radius; ++z) {
    for (i32 y = center.value.y - radius; y <= center.value.y + radius; ++y) {
      for (i32 x = center.value.x - radius; x <= center.value.x + radius; ++x) {
        chunks.insert({{x, y, z}});
      }
    }
  }
  return chunks;
}

void ServerChunkStreamer::compute_streaming_diff(
    PlayerChunkStreamState& state,
    HashSet<ChunkCoord> next_desired) {
  const HashSet<ChunkCoord> retained_chunks = compute_streaming_set(
      state.center_chunk,
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

  for (const ChunkCoord coord : next_desired) {
    if (state.sent_chunk_versions.find(coord) == state.sent_chunk_versions.end()) {
      state.pending_chunks.insert(coord);
    }
  }

  state.desired_chunks = std::move(next_desired);
}

void ServerChunkStreamer::queue_dirty_sent_chunks(
    PlayerChunkStreamState& state,
    const world::WorldRuntime& world) {
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
    ChunkCoord coord) const {
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
  u32 sent = 0;
  while (sent < config_.max_unloads_sent_per_player_tick && !state.unload_queue.empty()) {
    const ChunkCoord coord = state.unload_queue.back();
    state.unload_queue.pop_back();

    protocol::ChunkUnload unload;
    unload.world_id = state.world_id;
    unload.coord = coord;
    QueuedChunkStreamMessage message;
    message.session_id = state.session_id;
    message.connection_id = state.connection_id;
    message.world_id = state.world_id;
    message.reliability = protocol::ReliabilityClass::ReliableOrdered;
    message.lane = protocol::NetworkLane::ChunkStreaming;
    message.message_type = protocol::MessageType::ChunkUnload;
    message.payload = protocol::serialize_chunk_unload(unload);
    message.coord = coord;
    out_messages.emplace_back(std::move(message));
    ++sent;
  }
}

void ServerChunkStreamer::queue_chunk_sends(
    world::WorldRuntime& world,
    PlayerChunkStreamState& state,
    std::vector<QueuedChunkStreamMessage>& out_messages) {
  u32 sent = 0;
  HashSet<ChunkCoord> deferred_this_tick;
  while (sent < config_.max_chunks_sent_per_player_tick) {
    std::optional<ChunkCoord> next_coord;
    for (const ChunkCoord coord : state.pending_chunks) {
      if (deferred_this_tick.find(coord) != deferred_this_tick.end()) {
        continue;
      }
      if (!next_coord.has_value() ||
          chunk_order_less(coord, next_coord.value(), state.center_chunk)) {
        next_coord = coord;
      }
    }

    if (!next_coord.has_value()) {
      return;
    }

    const ChunkCoord coord = next_coord.value();
    if (state.desired_chunks.find(coord) == state.desired_chunks.end()) {
      state.pending_chunks.erase(coord);
      continue;
    }

    const std::optional<voxel::ChunkSnapshot> snapshot =
        prepare_chunk_snapshot(world, coord);
    if (!snapshot.has_value()) {
      deferred_this_tick.insert(coord);
      continue;
    }

    const voxel::ChunkSection* source_chunk = world.find_chunk(coord);
    if (source_chunk == nullptr) {
      state.pending_chunks.erase(coord);
      continue;
    }

    protocol::ChunkData data;
    data.world_id = state.world_id;
    data.coord = coord;
    data.chunk_version = snapshot->source_version;
    data.chunk_content_version = snapshot->source_content_version;
    data.chunk_light_version = source_chunk->light_data_version();
    data.has_lighting = true;
    data.lighting_valid = !source_chunk->is_dirty(voxel::ChunkDirtyFlags::Lighting);
    data.has_fluids = has_any_fluid(*source_chunk);
    data.blocks = snapshot->blocks();
    data.lights = snapshot->lights();
    if (data.has_fluids) {
      data.fluids = snapshot->fluids();
    } else {
      data.fluids.fill(voxel::empty_fluid_cell());
    }

    QueuedChunkStreamMessage message;
    message.session_id = state.session_id;
    message.connection_id = state.connection_id;
    message.world_id = state.world_id;
    message.reliability = protocol::ReliabilityClass::ReliableOrdered;
    message.lane = protocol::NetworkLane::ChunkStreaming;
    message.message_type = protocol::MessageType::ChunkData;
    message.payload = protocol::serialize_chunk_data(data);
    message.coord = coord;
    message.block_version = snapshot->source_version;
    message.light_version = source_chunk->light_data_version();
    message.lighting_valid = data.lighting_valid;
    message.tracks_sent_chunk = true;
    out_messages.emplace_back(std::move(message));
    deferred_this_tick.insert(coord);
    ++sent;
  }
}

} // namespace arv::server
