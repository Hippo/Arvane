#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/net/transport.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/server/session_registry.hpp"
#include "arv/world/world_generation.hpp"
#include "arv/world/world_runtime.hpp"

#include <optional>
#include <vector>

namespace arv::server {

struct ServerChunkStreamingConfig {
  i32 max_view_distance{4};
  i32 simulation_distance{1};
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
  SessionID session_id{};
  net::ConnectionID connection_id{};
  WorldID world_id{};
  protocol::ReliabilityClass reliability{protocol::ReliabilityClass::ReliableOrdered};
  protocol::NetworkLane lane{protocol::NetworkLane::ChunkStreaming};
  protocol::MessageType message_type{protocol::MessageType::ClientboundChunkSectionData};
  std::vector<u8> payload{};
  ChunkSectionCoord coord{};
  u64 block_version{};
  u64 light_version{};
  bool lighting_valid{};
  bool tracks_sent_chunk{};
};

class ServerChunkStreamer {
public:
  void configure(ServerChunkStreamingConfig config) noexcept;
  void update_player_interest(
      const ServerSession& session,
      ChunkSectionCoord authoritative_center,
      world::WorldHeightSettings height,
      const protocol::ServerboundChunkInterestUpdate& interest);
  void update_player_interest(
      const ServerSession& session,
      ChunkSectionCoord authoritative_center,
      const protocol::ServerboundChunkInterestUpdate& interest) {
    update_player_interest(session, authoritative_center, {}, interest);
  }
  void remove_session(SessionID session_id);
  void tick_streaming_budget(
      world::WorldRuntime& world,
      std::vector<QueuedChunkStreamMessage>& out_messages);
  [[nodiscard]] std::vector<ChunkCoord> pending_generation_requests(
      const world::WorldRuntime& world) const;
  [[nodiscard]] bool session_has_sent_chunk(
      SessionID session_id,
      WorldID world_id,
      ChunkSectionCoord coord) const noexcept;
  bool mark_sent_block_version(
      SessionID session_id,
      WorldID world_id,
      ChunkSectionCoord coord,
      u64 block_version) noexcept;
  bool confirm_chunk_sent(
      SessionID session_id,
      WorldID world_id,
      ChunkSectionCoord coord,
      u64 block_version,
      u64 light_version,
      bool lighting_valid) noexcept;

  [[nodiscard]] usize tracked_player_count() const noexcept {
    return players_.size();
  }

private:
  struct SentChunkVersions {
    u64 block_version{};
    u64 light_version{};
    bool lighting_valid_sent{};
  };

  struct PlayerChunkStreamState {
    SessionID session_id{};
    PlayerID player_id{};
    net::ConnectionID connection_id{};
    WorldID world_id{};
    ChunkSectionCoord center_chunk{};
    i32 view_distance{};
    HashSet<ChunkSectionCoord> desired_chunks{};
    HashMap<ChunkSectionCoord, SentChunkVersions> sent_chunk_versions{};
    HashSet<ChunkSectionCoord> pending_chunks{};
    std::vector<ChunkSectionCoord> unload_queue{};
  };

  [[nodiscard]] i32 clamp_view_distance(i32 requested_distance) const noexcept;
  [[nodiscard]] HashSet<ChunkSectionCoord> compute_streaming_set(
      ChunkSectionCoord center,
      world::WorldHeightSettings height,
      i32 radius) const;
  void compute_streaming_diff(
      PlayerChunkStreamState& state,
      world::WorldHeightSettings height,
      HashSet<ChunkSectionCoord> next_desired);
  void queue_dirty_sent_chunks(PlayerChunkStreamState& state, const world::WorldRuntime& world);
  [[nodiscard]] std::optional<voxel::ChunkSnapshot> prepare_chunk_snapshot(
      world::WorldRuntime& world,
      ChunkSectionCoord coord) const;
  void queue_chunk_unloads(
      PlayerChunkStreamState& state,
      std::vector<QueuedChunkStreamMessage>& out_messages);
  void queue_chunk_sends(
      world::WorldRuntime& world,
      PlayerChunkStreamState& state,
      std::vector<QueuedChunkStreamMessage>& out_messages);

  ServerChunkStreamingConfig config_{};
  HashMap<SessionID, PlayerChunkStreamState> players_;
};

} // namespace arv::server
