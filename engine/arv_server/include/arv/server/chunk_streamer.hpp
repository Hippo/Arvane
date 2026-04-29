#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/net/transport.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/server/session_registry.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

#include <unordered_map>
#include <unordered_set>
#include <optional>
#include <vector>

namespace arv::server {

struct ServerChunkStreamingConfig {
  i32 max_view_distance{4};
  i32 unload_margin{1};
  u32 max_chunks_sent_per_player_tick{4};
  u32 max_unloads_sent_per_player_tick{64};
};

struct ServerChunkGenerationSettings {
  bool enabled{};
  u64 seed{};
  world::WorldGenerationConfig config{};
  world::WorldGenerationPalette palette{};
};

struct QueuedChunkStreamMessage {
  net::ConnectionID connection_id{};
  protocol::ReliabilityClass reliability{protocol::ReliabilityClass::ReliableOrdered};
  protocol::MessageType message_type{protocol::MessageType::ChunkData};
  std::vector<u8> payload{};
};

class ServerChunkStreamer {
public:
  void configure(ServerChunkStreamingConfig config) noexcept;
  void update_player_interest(
      const ServerSession& session,
      ChunkCoord authoritative_center,
      const protocol::ChunkInterestUpdate& interest);
  void remove_session(SessionID session_id);
  void tick_streaming_budget(
      world::WorldRuntime& world,
      std::vector<QueuedChunkStreamMessage>& out_messages);
  [[nodiscard]] std::vector<ChunkCoord> pending_generation_requests(
      const world::WorldRuntime& world) const;

  [[nodiscard]] usize tracked_player_count() const noexcept {
    return players_.size();
  }

private:
  struct SentChunkVersions {
    u64 block_version{};
    u64 light_version{};
  };

  struct PlayerChunkStreamState {
    SessionID session_id{};
    PlayerID player_id{};
    net::ConnectionID connection_id{};
    WorldID world_id{};
    ChunkCoord center_chunk{};
    i32 view_distance{};
    std::unordered_set<ChunkCoord> desired_chunks{};
    std::unordered_map<ChunkCoord, SentChunkVersions> sent_chunk_versions{};
    std::unordered_set<ChunkCoord> pending_chunks{};
    std::vector<ChunkCoord> unload_queue{};
  };

  [[nodiscard]] i32 clamp_view_distance(i32 requested_distance) const noexcept;
  [[nodiscard]] std::unordered_set<ChunkCoord> compute_streaming_set(
      ChunkCoord center,
      i32 radius) const;
  void compute_streaming_diff(
      PlayerChunkStreamState& state,
      std::unordered_set<ChunkCoord> next_desired);
  void queue_dirty_sent_chunks(PlayerChunkStreamState& state, const world::WorldRuntime& world);
  [[nodiscard]] std::optional<voxel::ChunkSnapshot> prepare_chunk_snapshot(
      world::WorldRuntime& world,
      ChunkCoord coord) const;
  void queue_chunk_unloads(
      PlayerChunkStreamState& state,
      std::vector<QueuedChunkStreamMessage>& out_messages);
  void queue_chunk_sends(
      world::WorldRuntime& world,
      PlayerChunkStreamState& state,
      std::vector<QueuedChunkStreamMessage>& out_messages);

  ServerChunkStreamingConfig config_{};
  std::unordered_map<SessionID, PlayerChunkStreamState> players_;
};

} // namespace arv::server
