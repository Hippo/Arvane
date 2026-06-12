#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"
#include "arv/crypto/ed25519.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/net/connection_state.hpp"
#include "arv/net/transport.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_updates.hpp"
#include "arv/protocol/pack_sync.hpp"
#include "arv/protocol/player_movement.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace arv::client {

struct ClientSessionIdentity {
  std::filesystem::path arvane_home{};
  std::string display_name{"local_player"};
  crypto::PlayerProfileID profile_id{};
  crypto::Ed25519KeyPair key_pair{};
};

struct ClientSessionWorldJoin {
  WorldID world_id{};
  PlayerID player_id{};
  EntityNetID local_entity_id{};
  Vec3f spawn_position{};
  u64 initial_tick{};
  u32 tick_rate{};
  i32 effective_view_distance{};
};

struct ClientSessionEntityMirror {
  WorldID world_id{};
  EntityNetID entity_id{};
  EntityTypeID entity_type{};
  std::string entity_type_name{};
  Vec3f position{};
  Vec3f velocity{};
  Quatf rotation{};
  u32 spawn_flags{};
  u32 state_flags{};
  Vec3f head_rotation_degrees{};
  f32 health_current{};
  f32 health_max{};
  std::optional<PlayerID> owning_player{};
  u64 last_server_tick{};
};

[[nodiscard]] ClientSessionIdentity make_ephemeral_client_session_identity(
    std::string display_name = "local_player");

class ClientSession {
public:
  explicit ClientSession(
      std::unique_ptr<net::ITransport> transport,
      ClientSessionIdentity identity = make_ephemeral_client_session_identity());

  ClientSession(const ClientSession&) = delete;
  ClientSession& operator=(const ClientSession&) = delete;

  [[nodiscard]] net::ConnectResult connect(const net::NetworkAddress& address);
  void pump();
  void disconnect(std::string_view reason = {});

  [[nodiscard]] net::ConnectionState connection_state() const noexcept {
    return connection_state_;
  }

  [[nodiscard]] const std::optional<std::string>& disconnect_reason() const noexcept {
    return disconnect_reason_;
  }

  [[nodiscard]] const ClientSessionIdentity& identity() const noexcept {
    return identity_;
  }

  [[nodiscard]] const std::string& registry_snapshot_json() const noexcept {
    return registry_snapshot_json_;
  }

  [[nodiscard]] std::span<const std::filesystem::path> multiplayer_pack_cache_paths()
      const noexcept {
    return loaded_bundle_paths_;
  }

  [[nodiscard]] const std::optional<ClientSessionWorldJoin>& world_join() const noexcept {
    return world_join_;
  }

  [[nodiscard]] const std::vector<protocol::ClientboundChunkSectionData>& chunk_sections()
      const noexcept {
    return chunk_sections_;
  }

  [[nodiscard]] std::span<const ClientSessionEntityMirror> entity_mirrors()
      const noexcept {
    return entity_mirrors_;
  }

  [[nodiscard]] std::optional<protocol::ClientboundPlayerMoveCorrection> take_player_move_correction();

  [[nodiscard]] bool submit_player_move(protocol::ServerboundPlayerMove move);

private:
  struct PackDownload {
    protocol::PackBundleDescriptor descriptor{};
    std::vector<u8> bytes{};
    std::unordered_set<u32> received_chunks{};
    u64 received_bytes{};
    u32 chunk_count{};
  };

  [[nodiscard]] bool send_message(
      protocol::MessageType type,
      std::vector<u8> payload,
      protocol::ReliabilityClass reliability = protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane lane = protocol::NetworkLane::Session);
  void send_pack_sync_failure(std::string reason, std::string failed_hash = {});
  void send_loaded_pack_status();
  [[nodiscard]] std::filesystem::path pack_cache_root() const;
  [[nodiscard]] bool all_required_bundles_loaded() const;
  void remember_loaded_bundle(std::string hash);
  void handle_pack_manifest(protocol::ClientboundPackManifest manifest);
  void handle_pack_data_chunk(protocol::ClientboundPackDataChunk chunk);
  void complete_download(PackDownload& download);
  void send_chunk_interest(ChunkSectionCoord center, i32 view_distance);
  void handle_chunk_section_data(protocol::ClientboundChunkSectionData data);
  void handle_chunk_section_unload(protocol::ClientboundChunkSectionUnload unload);
  void handle_entity_spawn(protocol::ClientboundEntitySpawn spawn);
  void handle_entity_despawn(protocol::ClientboundEntityDespawn despawn);
  void handle_entity_snapshot(protocol::ClientboundEntitySnapshot snapshot);
  void handle_message(std::span<const u8> payload);
  void handle_connection_closed(std::string reason);

  std::unique_ptr<net::ITransport> transport_{};
  ClientSessionIdentity identity_{};
  net::ConnectionID connection_id_{};
  net::ConnectionState connection_state_{net::ConnectionState::Disconnected};
  std::optional<std::string> disconnect_reason_{};
  std::string expected_registry_hash_{};
  std::string registry_snapshot_json_{};
  std::optional<ClientSessionWorldJoin> world_join_{};
  std::optional<bool> server_requires_pack_sync_{};
  std::unordered_set<std::string> required_bundle_hashes_{};
  std::vector<std::string> loaded_bundle_hashes_{};
  std::vector<std::filesystem::path> loaded_bundle_paths_{};
  std::unordered_map<std::string, PackDownload> downloads_{};
  std::vector<protocol::ClientboundChunkSectionData> chunk_sections_{};
  std::vector<ClientSessionEntityMirror> entity_mirrors_{};
  std::optional<protocol::ClientboundPlayerMoveCorrection> latest_player_move_correction_{};
  u32 next_chunk_interest_sequence_{1};
  u32 next_player_move_sequence_{1};
  std::optional<ChunkSectionCoord> previous_chunk_interest_center_{};
};

} // namespace arv::client
