#include "arv/client/client_session.hpp"

#include "arv/asset/pack_bundle_cache.hpp"
#include "arv/client/client_pack_sync.hpp"
#include "arv/crypto/crypto_init.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/entity_updates.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/pack_sync.hpp"
#include "arv/protocol/player_movement.hpp"
#include "arv/protocol/session_control.hpp"
#include "arv/protocol/world_join.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <system_error>
#include <utility>

namespace arv::client {
namespace {

[[nodiscard]] std::span<const std::byte> text_bytes(std::string_view text) noexcept {
  return {
      reinterpret_cast<const std::byte*>(text.data()),
      text.size()};
}

[[nodiscard]] std::string hash_text_hex(std::string_view text) {
  return crypto::hash_256_hex(crypto::hash_256(text_bytes(text)));
}

[[nodiscard]] crypto::PlayerProfileID profile_id_for(
    const ClientSessionIdentity& identity) {
  return identity.profile_id.empty()
      ? crypto::derive_profile_id_from_public_key(identity.key_pair.public_key)
      : identity.profile_id;
}

[[nodiscard]] bool verify_identity_keypair(
    const ClientSessionIdentity& identity) {
  constexpr std::array<std::byte, 17> message{
      std::byte{'a'}, std::byte{'r'}, std::byte{'v'}, std::byte{'a'},
      std::byte{'n'}, std::byte{'e'}, std::byte{'-'}, std::byte{'a'},
      std::byte{'u'}, std::byte{'t'}, std::byte{'h'}, std::byte{'-'},
      std::byte{'s'}, std::byte{'m'}, std::byte{'o'}, std::byte{'k'},
      std::byte{'e'}};
  const crypto::Ed25519Signature signature =
      crypto::sign_ed25519(message, identity.key_pair.private_key);
  return crypto::verify_ed25519(
      message,
      signature,
      identity.key_pair.public_key);
}

[[nodiscard]] bool contains_string(
    std::span<const std::string> values,
    std::string_view value) noexcept {
  return std::find(values.begin(), values.end(), value) != values.end();
}

[[nodiscard]] bool matches_joined_or_pending_world(
    const std::optional<ClientSessionWorldJoin>& world_join,
    WorldID packet_world) noexcept {
  return !world_join.has_value() || packet_world == world_join->world_id;
}

[[nodiscard]] bool is_message_allowed_for_state(
    protocol::MessageType type,
    net::ConnectionState state) noexcept {
  switch (type) {
    case protocol::MessageType::ClientboundAuthChallenge:
    case protocol::MessageType::ClientboundAuthResult:
      return state == net::ConnectionState::ProtocolHandshake ||
             state == net::ConnectionState::RegistrySync ||
             state == net::ConnectionState::WorldJoin;
    case protocol::MessageType::ClientboundHello:
      return state == net::ConnectionState::TransportConnected ||
             state == net::ConnectionState::ProtocolHandshake;
    case protocol::MessageType::ClientboundPackManifest:
    case protocol::MessageType::ClientboundPackDataChunk:
    case protocol::MessageType::ClientboundRegistrySnapshot:
      return state == net::ConnectionState::RegistrySync;
    case protocol::MessageType::ClientboundWorldJoinResult:
      return state == net::ConnectionState::WorldJoin;
    case protocol::MessageType::ClientboundChunkSectionData:
    case protocol::MessageType::ClientboundChunkSectionUnload:
    case protocol::MessageType::ClientboundBlockUpdateBatch:
    case protocol::MessageType::ClientboundEntitySpawn:
    case protocol::MessageType::ClientboundEntityDespawn:
    case protocol::MessageType::ClientboundEntitySnapshot:
    case protocol::MessageType::ClientboundDroppedItemSnapshot:
      return state == net::ConnectionState::RegistrySync ||
             state == net::ConnectionState::WorldJoin ||
             state == net::ConnectionState::InGame;
    case protocol::MessageType::ClientboundInventoryUpdate:
    case protocol::MessageType::ClientboundPlayerBlockBreakProgress:
      return state == net::ConnectionState::RegistrySync ||
             state == net::ConnectionState::WorldJoin ||
             state == net::ConnectionState::InGame;
    case protocol::MessageType::ClientboundPlayerMoveCorrection:
      return state == net::ConnectionState::InGame;
    default:
      return true;
  }
}

[[nodiscard]] std::string wrong_state_reason(
    protocol::MessageType type,
    net::ConnectionState state) {
  std::ostringstream out;
  out << "server sent packet 0x"
      << std::hex
      << static_cast<unsigned int>(type)
      << " in session state "
      << static_cast<unsigned int>(state);
  return out.str();
}

[[nodiscard]] bool write_temp_apack_file(
    const std::filesystem::path& path,
    std::span<const u8> bytes,
    std::string& error_message) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) {
    error_message = "Could not create the server pack cache.";
    return false;
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    error_message = "Could not write the downloaded server pack.";
    return false;
  }
  if (!bytes.empty()) {
    output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
  }
  output.flush();
  if (!output) {
    error_message = "Could not finish writing the downloaded server pack.";
    return false;
  }
  return true;
}

} // namespace

ClientSessionIdentity make_ephemeral_client_session_identity(std::string display_name) {
  ClientSessionIdentity identity;
  identity.display_name = std::move(display_name);
  identity.key_pair = crypto::generate_ed25519_keypair();
  identity.profile_id = crypto::derive_profile_id_from_public_key(identity.key_pair.public_key);
  return identity;
}

ClientSession::ClientSession(
    std::unique_ptr<net::ITransport> transport,
    ClientSessionIdentity identity)
    : transport_(std::move(transport)),
      identity_(std::move(identity)) {
}

net::ConnectResult ClientSession::connect(const net::NetworkAddress& address) {
  if (transport_ == nullptr) {
    return net::ConnectResult::failure(net::NetworkError{"client session has no transport"});
  }
  if (!crypto::initialize_crypto()) {
    return net::ConnectResult::failure(net::NetworkError{"crypto initialization failed"});
  }
  if (!verify_identity_keypair(identity_)) {
    return net::ConnectResult::failure(
        net::NetworkError{"client identity key self-check failed"});
  }
  auto result = transport_->connect(address);
  if (!result) {
    connection_state_ = net::ConnectionState::Disconnected;
    disconnect_reason_ = result.error().message;
    return result;
  }
  connection_id_ = result.value();
  connection_state_ = net::ConnectionState::Connecting;
  disconnect_reason_.reset();
  registry_snapshot_json_.clear();
  expected_registry_hash_.clear();
  world_join_.reset();
  server_requires_pack_sync_.reset();
  chunk_sections_.clear();
  entity_mirrors_.clear();
  dropped_items_.clear();
  inventory_ = player::Inventory{player::DefaultPlayerInventorySlotCount};
  inventory_cursor_stack_.reset();
  inventory_revision_ = 0;
  has_authoritative_inventory_ = false;
  block_break_progress_ = {};
  latest_player_move_correction_.reset();
  previous_chunk_interest_center_.reset();
  next_chunk_interest_sequence_ = 1;
  next_player_move_sequence_ = 1;
  required_bundle_hashes_.clear();
  loaded_bundle_hashes_.clear();
  loaded_bundle_paths_.clear();
  downloads_.clear();
  return result;
}

void ClientSession::disconnect(std::string_view reason) {
  if (transport_ != nullptr) {
    (void)transport_->close_connection(connection_id_, reason);
  }
  handle_connection_closed(std::string{reason});
}

void ClientSession::pump() {
  if (transport_ == nullptr) {
    return;
  }
  std::vector<net::NetworkEvent> events = transport_->poll_events();
  for (net::NetworkEvent& event : events) {
    switch (event.type) {
      case net::NetworkEventType::ConnectionAccepted: {
        if (connection_state_ != net::ConnectionState::Connecting) {
          break;
        }
        connection_state_ = net::ConnectionState::TransportConnected;
        protocol::ServerboundHello hello;
        hello.requested_username_or_profile_name = identity_.display_name;
        hello.auth_begin = protocol::ServerboundAuthBegin{
            identity_.key_pair.public_key,
            identity_.display_name,
            profile_id_for(identity_)};
        if (!send_message(
                protocol::MessageType::ServerboundHello,
                protocol::serialize_serverbound_hello(hello))) {
          disconnect("failed to send client hello");
          break;
        }
        connection_state_ = net::ConnectionState::ProtocolHandshake;
        break;
      }
      case net::NetworkEventType::ConnectionClosed:
      case net::NetworkEventType::ConnectionFailed:
        handle_connection_closed(event.reason);
        break;
      case net::NetworkEventType::MessageReceived:
        handle_message(event.payload);
        break;
      case net::NetworkEventType::ConnectionRequested:
      case net::NetworkEventType::MessageSendFailed:
        break;
    }
  }
}

bool ClientSession::send_message(
    protocol::MessageType type,
    std::vector<u8> payload,
    protocol::ReliabilityClass reliability,
    protocol::NetworkLane lane) {
  if (transport_ == nullptr) {
    return false;
  }
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, type, 0, 0, 0, 0},
      payload);
  auto result = transport_->send({
      connection_id_,
      reliability,
      std::move(packet),
      type,
      lane});
  return static_cast<bool>(result);
}

bool ClientSession::submit_player_move(protocol::ServerboundPlayerMove move) {
  if (connection_state_ != net::ConnectionState::InGame || !world_join_.has_value()) {
    return false;
  }
  move.sequence = next_player_move_sequence_;
  if (!send_message(
          protocol::MessageType::ServerboundPlayerMove,
          protocol::serialize_serverbound_player_move(move),
          protocol::ReliabilityClass::UnreliableLatestWins,
          protocol::NetworkLane::Movement)) {
    return false;
  }
  ++next_player_move_sequence_;
  return true;
}

std::optional<protocol::ClientboundPlayerMoveCorrection>
ClientSession::take_player_move_correction() {
  std::optional<protocol::ClientboundPlayerMoveCorrection> correction =
      std::move(latest_player_move_correction_);
  latest_player_move_correction_.reset();
  return correction;
}

std::optional<protocol::ClientboundPlayerVelocity> ClientSession::take_player_velocity() {
  std::optional<protocol::ClientboundPlayerVelocity> velocity =
      std::move(latest_player_velocity_);
  latest_player_velocity_.reset();
  return velocity;
}

std::optional<protocol::ClientboundSetMovementHz> ClientSession::take_movement_settings() {
  std::optional<protocol::ClientboundSetMovementHz> settings =
      std::move(latest_movement_settings_);
  latest_movement_settings_.reset();
  return settings;
}

std::optional<protocol::ClientboundPlayerPermissions> ClientSession::take_player_permissions() {
  std::optional<protocol::ClientboundPlayerPermissions> permissions =
      std::move(latest_player_permissions_);
  latest_player_permissions_.reset();
  return permissions;
}

std::optional<protocol::ClientboundPlayerMovementTuning>
ClientSession::take_player_movement_tuning() {
  std::optional<protocol::ClientboundPlayerMovementTuning> tuning =
      std::move(latest_player_movement_tuning_);
  latest_player_movement_tuning_.reset();
  return tuning;
}

void ClientSession::send_pack_sync_failure(std::string reason, std::string failed_hash) {
  protocol::ServerboundPackSyncStatus status;
  status.status = protocol::PackSyncStatus::VerificationFailed;
  if (!failed_hash.empty()) {
    status.failed_bundle_hashes.push_back(std::move(failed_hash));
  }
  status.failure_reason = reason;
  (void)send_message(
      protocol::MessageType::ServerboundPackSyncStatus,
      protocol::serialize_serverbound_pack_sync_status(status));
  disconnect(reason);
}

void ClientSession::send_loaded_pack_status() {
  protocol::ServerboundPackSyncStatus status;
  status.status = protocol::PackSyncStatus::Loaded;
  status.already_cached_bundle_hashes = loaded_bundle_hashes_;
  (void)send_message(
      protocol::MessageType::ServerboundPackSyncStatus,
      protocol::serialize_serverbound_pack_sync_status(status));
}

std::filesystem::path ClientSession::pack_cache_root() const {
  std::filesystem::path root = identity_.arvane_home;
  if (root.empty()) {
    root = std::filesystem::temp_directory_path() / "arvane_ephemeral_client";
  }
  return root / "cache" / "server-packs";
}

bool ClientSession::all_required_bundles_loaded() const {
  for (const std::string& hash : required_bundle_hashes_) {
    if (!contains_string(loaded_bundle_hashes_, hash)) {
      return false;
    }
  }
  return true;
}

void ClientSession::remember_loaded_bundle(std::string hash) {
  if (!contains_string(loaded_bundle_hashes_, hash)) {
    loaded_bundle_hashes_.push_back(std::move(hash));
  }
}

void ClientSession::handle_pack_manifest(protocol::ClientboundPackManifest manifest) {
  expected_registry_hash_.clear();
  registry_snapshot_json_.clear();
  required_bundle_hashes_.clear();
  loaded_bundle_hashes_.clear();
  loaded_bundle_paths_.clear();
  downloads_.clear();

  auto plan = prepare_client_pack_sync_manifest(pack_cache_root(), std::move(manifest));
  if (!plan) {
    send_pack_sync_failure(
        plan.error().reason,
        plan.error().failed_hash);
    return;
  }

  ClientPackSyncManifestPlan manifest_plan = std::move(plan.value());
  expected_registry_hash_ = std::move(manifest_plan.registry_hash);
  for (std::string& hash : manifest_plan.required_bundle_hashes) {
    required_bundle_hashes_.insert(std::move(hash));
  }
  for (std::string& hash : manifest_plan.loaded_bundle_hashes) {
    remember_loaded_bundle(std::move(hash));
  }
  for (std::filesystem::path& path : manifest_plan.loaded_bundle_paths) {
    loaded_bundle_paths_.push_back(std::move(path));
  }
  for (ClientPackSyncDownloadPlan& download_plan : manifest_plan.downloads) {
    PackDownload download;
    download.descriptor = std::move(download_plan.descriptor);
    download.bytes.resize(static_cast<usize>(download.descriptor.compressed_size));
    downloads_.emplace(download.descriptor.bundle_hash, std::move(download));
  }

  protocol::ServerboundPackSyncStatus status = std::move(manifest_plan.status);
  const std::vector<std::string> missing_hashes =
      std::move(manifest_plan.missing_bundle_hashes);
  if (!send_message(
          protocol::MessageType::ServerboundPackSyncStatus,
          protocol::serialize_serverbound_pack_sync_status(status))) {
    disconnect("failed to send pack sync status");
    return;
  }
  connection_state_ = net::ConnectionState::RegistrySync;
  for (const std::string& hash : missing_hashes) {
    if (!send_message(
            protocol::MessageType::ServerboundPackRequest,
            protocol::serialize_serverbound_pack_request({hash}))) {
      disconnect("failed to request server pack bundle");
      return;
    }
  }
}

void ClientSession::handle_pack_data_chunk(protocol::ClientboundPackDataChunk chunk) {
  auto found = downloads_.find(chunk.bundle_hash);
  if (found == downloads_.end()) {
    return;
  }
  PackDownload& download = found->second;
  if (chunk.chunk_count == 0U ||
      chunk.chunk_index >= chunk.chunk_count ||
      (download.chunk_count != 0U && download.chunk_count != chunk.chunk_count) ||
      chunk.byte_offset > download.bytes.size() ||
      chunk.bytes.size() > download.bytes.size() - static_cast<usize>(chunk.byte_offset)) {
    send_pack_sync_failure(
        "Server sent an invalid pack bundle chunk.",
        chunk.bundle_hash);
    return;
  }
  if (download.received_chunks.find(chunk.chunk_index) != download.received_chunks.end()) {
    return;
  }
  download.chunk_count = chunk.chunk_count;
  std::copy(
      chunk.bytes.begin(),
      chunk.bytes.end(),
      download.bytes.begin() + static_cast<std::ptrdiff_t>(chunk.byte_offset));
  download.received_chunks.insert(chunk.chunk_index);
  download.received_bytes += chunk.bytes.size();
  if (download.received_chunks.size() == download.chunk_count &&
      download.received_bytes == download.bytes.size()) {
    complete_download(download);
  }
}

void ClientSession::complete_download(PackDownload& download) {
  const std::filesystem::path cache_root = pack_cache_root();
  const std::filesystem::path temp_path =
      cache_root / "tmp" / (download.descriptor.bundle_hash + ".apack");
  std::string write_error;
  if (!write_temp_apack_file(temp_path, download.bytes, write_error)) {
    send_pack_sync_failure(
        write_error,
        download.descriptor.bundle_hash);
    return;
  }

  auto install = asset::install_client_pack_bundle_from_file(
      temp_path,
      cache_root,
      download.descriptor.bundle_hash);
  std::error_code cleanup_error;
  std::filesystem::remove(temp_path, cleanup_error);
  if (!install) {
    send_pack_sync_failure(
        install.error().message,
        download.descriptor.bundle_hash);
    return;
  }

  remember_loaded_bundle(install.value().bundle_hash);
  loaded_bundle_paths_.push_back(install.value().cache_path);
  downloads_.erase(download.descriptor.bundle_hash);
  send_loaded_pack_status();
}

void ClientSession::send_chunk_interest(ChunkSectionCoord center, i32 view_distance) {
  if (!world_join_.has_value()) {
    return;
  }
  protocol::ServerboundChunkInterestUpdate update;
  update.world_id = world_join_->world_id;
  update.center_chunk = center;
  update.client_view_distance = view_distance < 0 ? 0 : view_distance;
  update.previous_center_chunk = previous_chunk_interest_center_;
  update.sequence_id = next_chunk_interest_sequence_++;
  if (send_message(
          protocol::MessageType::ServerboundChunkInterestUpdate,
          protocol::serialize_serverbound_chunk_interest_update(update),
          protocol::ReliabilityClass::ReliableOrdered,
          protocol::NetworkLane::Gameplay)) {
    previous_chunk_interest_center_ = center;
  }
}

void ClientSession::handle_chunk_section_data(protocol::ClientboundChunkSectionData data) {
  if (!world_join_.has_value() || data.world_id != world_join_->world_id) {
    return;
  }
  (void)chunk_sections_.apply_section_data(world_join_->world_id, std::move(data));
}

void ClientSession::handle_chunk_section_unload(protocol::ClientboundChunkSectionUnload unload) {
  if (!world_join_.has_value() || unload.world_id != world_join_->world_id) {
    return;
  }
  (void)chunk_sections_.apply_section_unload(world_join_->world_id, unload);
}

void ClientSession::handle_block_update_batch(protocol::ClientboundBlockUpdateBatch batch) {
  if (!world_join_.has_value() || batch.world_id != world_join_->world_id) {
    return;
  }
  (void)chunk_sections_.apply_block_update_batch(world_join_->world_id, batch);
}

void ClientSession::handle_entity_spawn(protocol::ClientboundEntitySpawn spawn) {
  if (!matches_joined_or_pending_world(world_join_, spawn.world_id)) {
    return;
  }
  auto found = std::find_if(
      entity_mirrors_.begin(),
      entity_mirrors_.end(),
      [&](const ClientSessionEntityMirror& mirror) {
        return mirror.entity_id == spawn.entity_id;
      });
  ClientSessionEntityMirror mirror;
  mirror.world_id = spawn.world_id;
  mirror.entity_id = spawn.entity_id;
  mirror.entity_type = spawn.entity_type;
  mirror.entity_type_name = std::move(spawn.entity_type_name);
  mirror.position = spawn.position;
  mirror.velocity = spawn.velocity;
  mirror.rotation = spawn.rotation;
  mirror.spawn_flags = spawn.spawn_flags;
  mirror.owning_player = spawn.owning_player;
  if (found == entity_mirrors_.end()) {
    entity_mirrors_.push_back(std::move(mirror));
  } else {
    *found = std::move(mirror);
  }
}

void ClientSession::handle_entity_despawn(protocol::ClientboundEntityDespawn despawn) {
  if (!matches_joined_or_pending_world(world_join_, despawn.world_id)) {
    return;
  }
  const auto new_end = std::remove_if(
      entity_mirrors_.begin(),
      entity_mirrors_.end(),
      [&](const ClientSessionEntityMirror& mirror) {
        return mirror.entity_id == despawn.entity_id;
      });
  entity_mirrors_.erase(new_end, entity_mirrors_.end());
}

void ClientSession::handle_entity_snapshot(protocol::ClientboundEntitySnapshot snapshot) {
  if (!matches_joined_or_pending_world(world_join_, snapshot.world_id)) {
    return;
  }
  for (protocol::EntitySnapshotEntry& entry : snapshot.entities) {
    auto found = std::find_if(
        entity_mirrors_.begin(),
        entity_mirrors_.end(),
        [&](const ClientSessionEntityMirror& mirror) {
          return mirror.entity_id == entry.entity_id;
        });
    if (found == entity_mirrors_.end() ||
        snapshot.server_tick < found->last_server_tick) {
      continue;
    }
    found->position = entry.position;
    found->velocity = entry.velocity;
    found->rotation = entry.rotation;
    found->state_flags = entry.state_flags;
    found->animations = std::move(entry.animations);
    found->head_rotation_degrees = entry.head_rotation_degrees;
    found->health_current = entry.health_current;
    found->health_max = entry.health_max;
    found->last_server_tick = snapshot.server_tick;
  }
}

void ClientSession::handle_dropped_item_snapshot(
    protocol::ClientboundDroppedItemSnapshot snapshot) {
  if (!world_join_.has_value() || snapshot.world_id != world_join_->world_id) {
    return;
  }
  dropped_items_.apply_snapshot(
      snapshot,
      world_join_->world_id,
      world_join_->tick_rate == 0U ? DefaultBaseTps : world_join_->tick_rate);
}

void ClientSession::handle_inventory_update(
    protocol::ClientboundInventoryUpdate update) {
  if (update.inventory_id != protocol::PlayerInventoryID ||
      update.revision <= inventory_revision_) {
    return;
  }

  inventory_ = player::Inventory{update.slots.size()};
  for (usize index = 0; index < update.slots.size(); ++index) {
    (void)inventory_.set_slot(index, update.slots[index]);
  }
  inventory_cursor_stack_ = std::move(update.carried_stack);
  inventory_revision_ = update.revision;
  has_authoritative_inventory_ = true;
}

void ClientSession::handle_block_break_progress(
    protocol::ClientboundPlayerBlockBreakProgress progress) {
  if (world_join_.has_value() && progress.player_id != world_join_->player_id) {
    return;
  }
  block_break_progress_ = progress.block_break;
}

void ClientSession::handle_message(std::span<const u8> payload) {
  auto decoded = protocol::deserialize_envelope(payload);
  if (!decoded) {
    disconnect("invalid protocol packet");
    return;
  }
  if (connection_state_ == net::ConnectionState::Disconnected ||
      connection_state_ == net::ConnectionState::Disconnecting) {
    return;
  }
  if (!is_message_allowed_for_state(
          decoded.value().envelope.message_type,
          connection_state_)) {
    disconnect(wrong_state_reason(
        decoded.value().envelope.message_type,
        connection_state_));
    return;
  }

  switch (decoded.value().envelope.message_type) {
    case protocol::MessageType::ClientboundKeepAlive: {
      auto keep_alive = protocol::deserialize_clientbound_keep_alive(
          decoded.value().payload);
      if (!keep_alive) {
        disconnect("invalid keep alive");
        return;
      }
      if (!send_message(
              protocol::MessageType::ServerboundKeepAlive,
              protocol::serialize_serverbound_keep_alive({keep_alive.value().id}),
              protocol::ReliabilityClass::UnreliableEvent,
              protocol::NetworkLane::Session)) {
        disconnect("failed to send keep alive");
      }
      return;
    }
    case protocol::MessageType::ClientboundAuthChallenge: {
      auto challenge = protocol::deserialize_auth_challenge(decoded.value().payload);
      if (!challenge) {
        disconnect("invalid auth challenge");
        return;
      }
      const crypto::PlayerProfileID profile_id = profile_id_for(identity_);
      const std::vector<u8> signed_payload = protocol::build_auth_signed_payload({
          protocol::CurrentProtocolVersion,
          challenge.value().server_instance_id,
          challenge.value().session_id,
          profile_id,
          identity_.key_pair.public_key,
          challenge.value().nonce,
          challenge.value().expires_server_tick});
      const auto* first =
          reinterpret_cast<const std::byte*>(signed_payload.data());
      protocol::ServerboundAuthResponse response;
      response.signature = crypto::sign_ed25519(
          std::span<const std::byte>{first, signed_payload.size()},
          identity_.key_pair.private_key);
      if (!crypto::verify_ed25519(
              std::span<const std::byte>{first, signed_payload.size()},
              response.signature,
              identity_.key_pair.public_key)) {
        disconnect("auth signature self-check failed");
        return;
      }
      if (!send_message(
              protocol::MessageType::ServerboundAuthResponse,
              protocol::serialize_auth_response(response))) {
        disconnect("failed to send auth response");
      }
      return;
    }
    case protocol::MessageType::ClientboundAuthResult: {
      auto result = protocol::deserialize_auth_result(decoded.value().payload);
      if (!result) {
        disconnect("invalid auth result");
        return;
      }
      if (!result.value().accepted) {
        disconnect(result.value().reason_if_rejected.empty()
                       ? "authentication failed"
                       : result.value().reason_if_rejected);
      } else {
        if (connection_state_ == net::ConnectionState::ProtocolHandshake) {
          connection_state_ = server_requires_pack_sync_.value_or(true)
              ? net::ConnectionState::RegistrySync
              : net::ConnectionState::WorldJoin;
        }
      }
      return;
    }
    case protocol::MessageType::ClientboundHello: {
      auto hello = protocol::deserialize_clientbound_hello(decoded.value().payload);
      if (!hello) {
        disconnect("invalid server hello");
        return;
      }
      if (!hello.value().accepted) {
        disconnect(hello.value().reject_reason.empty()
                       ? "server rejected connection"
                       : hello.value().reject_reason);
        return;
      }
      server_requires_pack_sync_ = hello.value().required_followup_sync;
      connection_state_ = hello.value().required_followup_sync
          ? net::ConnectionState::RegistrySync
          : net::ConnectionState::WorldJoin;
      return;
    }
    case protocol::MessageType::ClientboundPackManifest: {
      auto manifest = protocol::deserialize_clientbound_pack_manifest(decoded.value().payload);
      if (!manifest) {
        disconnect("invalid pack manifest");
        return;
      }
      handle_pack_manifest(std::move(manifest.value()));
      return;
    }
    case protocol::MessageType::ClientboundPackDataChunk: {
      auto chunk = protocol::deserialize_clientbound_pack_data_chunk(decoded.value().payload);
      if (!chunk) {
        disconnect("invalid pack data chunk");
        return;
      }
      handle_pack_data_chunk(std::move(chunk.value()));
      return;
    }
    case protocol::MessageType::ClientboundRegistrySnapshot: {
      auto snapshot = protocol::deserialize_clientbound_registry_snapshot(
          decoded.value().payload);
      if (!snapshot) {
        disconnect("invalid registry snapshot");
        return;
      }
      if (snapshot.value().registry_hash != expected_registry_hash_ ||
          hash_text_hex(snapshot.value().registry_snapshot_payload) !=
              snapshot.value().registry_hash ||
          !all_required_bundles_loaded()) {
        send_pack_sync_failure(
            "Server registry snapshot did not match the pack manifest.");
        return;
      }
      registry_snapshot_json_ = std::move(snapshot.value().registry_snapshot_payload);
      protocol::ServerboundPackReady ready;
      ready.registry_hash = snapshot.value().registry_hash;
      ready.loaded_bundle_hashes = loaded_bundle_hashes_;
      if (!send_message(
              protocol::MessageType::ServerboundPackReady,
              protocol::serialize_serverbound_pack_ready(ready))) {
        disconnect("failed to send pack ready");
        return;
      }
      connection_state_ = net::ConnectionState::WorldJoin;
      return;
    }
    case protocol::MessageType::ClientboundWorldJoinResult: {
      auto join = protocol::deserialize_clientbound_world_join_result(
          decoded.value().payload);
      if (!join) {
        disconnect("invalid world join result");
        return;
      }
      if (!join.value().accepted) {
        disconnect(join.value().reject_reason.empty()
                       ? "server rejected world join"
                       : join.value().reject_reason);
        return;
      }
      world_join_ = ClientSessionWorldJoin{
          join.value().world_id,
          join.value().player_id,
          join.value().local_entity_id,
          join.value().spawn_position,
          join.value().initial_tick,
          join.value().tick_rate,
          join.value().effective_view_distance};
      block_break_progress_ = {};
      std::erase_if(
          entity_mirrors_,
          [&](const ClientSessionEntityMirror& mirror) {
            return mirror.world_id != world_join_->world_id;
          });
      connection_state_ = net::ConnectionState::InGame;
      const ChunkSectionCoord center{{
          floor_div(static_cast<i32>(std::floor(join.value().spawn_position.x)), ChunkSize),
          floor_div(static_cast<i32>(std::floor(join.value().spawn_position.y)), ChunkSize),
          floor_div(static_cast<i32>(std::floor(join.value().spawn_position.z)), ChunkSize),
      }};
      send_chunk_interest(center, join.value().effective_view_distance);
      return;
    }
    case protocol::MessageType::ClientboundChunkSectionData: {
      auto chunk = protocol::deserialize_clientbound_chunk_section_data(
          decoded.value().payload);
      if (!chunk) {
        disconnect("invalid chunk section data");
        return;
      }
      handle_chunk_section_data(std::move(chunk.value()));
      return;
    }
    case protocol::MessageType::ClientboundChunkSectionUnload: {
      auto unload = protocol::deserialize_clientbound_chunk_section_unload(
          decoded.value().payload);
      if (!unload) {
        disconnect("invalid chunk section unload");
        return;
      }
      handle_chunk_section_unload(unload.value());
      return;
    }
    case protocol::MessageType::ClientboundBlockUpdateBatch: {
      auto updates = protocol::deserialize_clientbound_block_update_batch(
          decoded.value().payload);
      if (!updates) {
        disconnect("invalid block update batch");
        return;
      }
      handle_block_update_batch(std::move(updates.value()));
      return;
    }
    case protocol::MessageType::ClientboundEntitySpawn: {
      auto spawn = protocol::deserialize_clientbound_entity_spawn(
          decoded.value().payload);
      if (!spawn) {
        disconnect("invalid entity spawn");
        return;
      }
      handle_entity_spawn(std::move(spawn.value()));
      return;
    }
    case protocol::MessageType::ClientboundEntityDespawn: {
      auto despawn = protocol::deserialize_clientbound_entity_despawn(
          decoded.value().payload);
      if (!despawn) {
        disconnect("invalid entity despawn");
        return;
      }
      handle_entity_despawn(despawn.value());
      return;
    }
    case protocol::MessageType::ClientboundEntitySnapshot: {
      auto snapshot = protocol::deserialize_clientbound_entity_snapshot(
          decoded.value().payload);
      if (!snapshot) {
        disconnect("invalid entity snapshot");
        return;
      }
      handle_entity_snapshot(std::move(snapshot.value()));
      return;
    }
    case protocol::MessageType::ClientboundDroppedItemSnapshot: {
      auto snapshot = protocol::deserialize_clientbound_dropped_item_snapshot(
          decoded.value().payload);
      if (!snapshot) {
        disconnect("invalid dropped item snapshot");
        return;
      }
      handle_dropped_item_snapshot(std::move(snapshot.value()));
      return;
    }
    case protocol::MessageType::ClientboundInventoryUpdate: {
      auto update = protocol::deserialize_clientbound_inventory_update(
          decoded.value().payload);
      if (!update) {
        disconnect("invalid inventory update");
        return;
      }
      handle_inventory_update(std::move(update.value()));
      return;
    }
    case protocol::MessageType::ClientboundPlayerBlockBreakProgress: {
      auto progress = protocol::deserialize_clientbound_player_block_break_progress(
          decoded.value().payload);
      if (!progress) {
        disconnect("invalid block break progress");
        return;
      }
      handle_block_break_progress(progress.value());
      return;
    }
    case protocol::MessageType::ClientboundPlayerVelocity: {
      auto velocity = protocol::deserialize_clientbound_player_velocity(
          decoded.value().payload);
      if (!velocity) {
        disconnect("invalid player velocity");
        return;
      }
      latest_player_velocity_ = velocity.value();
      return;
    }
    case protocol::MessageType::ClientboundSetMovementHz: {
      auto settings = protocol::deserialize_clientbound_set_movement_hz(
          decoded.value().payload);
      if (!settings) {
        disconnect("invalid movement settings");
        return;
      }
      latest_movement_settings_ = settings.value();
      return;
    }
    case protocol::MessageType::ClientboundPlayerPermissions: {
      auto permissions = protocol::deserialize_clientbound_player_permissions(
          decoded.value().payload);
      if (!permissions) {
        disconnect("invalid player permissions");
        return;
      }
      latest_player_permissions_ = std::move(permissions.value());
      return;
    }
    case protocol::MessageType::ClientboundPlayerMovementTuning: {
      auto tuning = protocol::deserialize_clientbound_player_movement_tuning(
          decoded.value().payload);
      if (!tuning) {
        disconnect("invalid player movement tuning");
        return;
      }
      latest_player_movement_tuning_ = tuning.value();
      return;
    }
    case protocol::MessageType::ClientboundPlayerMoveCorrection: {
      auto correction = protocol::deserialize_clientbound_player_move_correction(
          decoded.value().payload);
      if (!correction) {
        disconnect("invalid player move correction");
        return;
      }
      latest_player_move_correction_ = std::move(correction.value());
      return;
    }
    default:
      return;
  }
}

void ClientSession::handle_connection_closed(std::string reason) {
  connection_state_ = net::ConnectionState::Disconnected;
  disconnect_reason_ = std::move(reason);
  expected_registry_hash_.clear();
  world_join_.reset();
  chunk_sections_.clear();
  entity_mirrors_.clear();
  dropped_items_.clear();
  inventory_ = player::Inventory{player::DefaultPlayerInventorySlotCount};
  inventory_cursor_stack_.reset();
  inventory_revision_ = 0;
  has_authoritative_inventory_ = false;
  block_break_progress_ = {};
  latest_player_move_correction_.reset();
  latest_player_velocity_.reset();
  latest_movement_settings_.reset();
  latest_player_permissions_.reset();
  latest_player_movement_tuning_.reset();
  previous_chunk_interest_center_.reset();
  next_chunk_interest_sequence_ = 1;
  next_player_move_sequence_ = 1;
  required_bundle_hashes_.clear();
  loaded_bundle_hashes_.clear();
  loaded_bundle_paths_.clear();
  downloads_.clear();
}

} // namespace arv::client
