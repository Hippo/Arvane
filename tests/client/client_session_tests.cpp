#include "arv/asset/pack.hpp"
#include "arv/asset/pack_bundle.hpp"
#include "arv/asset/pack_bundle_cache.hpp"
#include "arv/client/client_session.hpp"
#include "arv/core/time.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/net/in_process_transport.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/block_updates.hpp"
#include "arv/protocol/entity_replication.hpp"
#include "arv/protocol/entity_updates.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/pack_sync.hpp"
#include "arv/protocol/player_input.hpp"
#include "arv/protocol/player_movement.hpp"
#include "arv/protocol/world_join.hpp"
#include "support/temp_dir.hpp"
#include "support/temp_files.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using arv::testsupport::ScopedTempDir;
using arv::testsupport::write_text;

void write_pack_manifest(
    const std::filesystem::path& packs_root,
    std::string_view id,
    std::string_view name_space) {
  write_text(
      packs_root / std::string{id} / "pack.yaml",
      "schema_version: 1\n"
      "id: \"" +
          std::string{id} + "\"\n"
                            "display_name: \"" +
          std::string{id} + "\"\n"
                            "version: \"1.0.0\"\n"
                            "namespaces:\n"
                            "  - \"" +
          std::string{name_space} + "\"\n"
                                    "side: \"both\"\n"
                                    "dependencies: []\n"
                                    "load_after: []\n"
                                    "load_before: []\n"
                                    "capabilities:\n"
                                    "  assets: true\n"
                                    "  data: true\n"
                                    "  native_plugin: false\n");
}

arv::protocol::PackBundleDescriptor descriptor_for(
    const arv::asset::ClientPackBundleBuildResult& bundle) {
  return {
      "sync_pack",
      "Sync Pack",
      "1.0.0",
      bundle.bundle_hash,
      static_cast<arv::u64>(bundle.apack_bytes.size()),
      bundle.uncompressed_size};
}

void send_to_client(
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id,
    arv::protocol::MessageType message_type,
    std::vector<arv::u8> payload) {
  std::vector<arv::u8> packet = arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion, message_type, 0, 0, 0, 0},
      payload);
  (void)server_transport.send(
      {connection_id,
       arv::protocol::ReliabilityClass::ReliableOrdered,
       std::move(packet),
       message_type,
       arv::protocol::NetworkLane::Session});
}

std::vector<arv::net::NetworkEvent> poll_messages(arv::net::ITransport& transport) {
  std::vector<arv::net::NetworkEvent> messages;
  for (arv::net::NetworkEvent& event : transport.poll_events()) {
    if (event.type == arv::net::NetworkEventType::MessageReceived) {
      messages.push_back(std::move(event));
    }
  }
  return messages;
}

arv::usize message_type_count(
    const std::vector<arv::net::NetworkEvent>& events,
    arv::protocol::MessageType type) {
  arv::usize count = 0U;
  for (const arv::net::NetworkEvent& event : events) {
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (decoded && decoded.value().envelope.message_type == type) {
      ++count;
    }
  }
  return count;
}

arv::usize transport_message_type_count(
    std::span<const arv::net::TransportMessage> messages,
    arv::protocol::MessageType type) {
  arv::usize count = 0U;
  for (const arv::net::TransportMessage& message : messages) {
    const auto decoded = arv::protocol::deserialize_envelope(message.payload);
    if (decoded && decoded.value().envelope.message_type == type) {
      ++count;
    }
  }
  return count;
}

template <typename Packet, typename Decode>
Packet decode_single_serverbound(
    const std::vector<arv::net::NetworkEvent>& events,
    arv::protocol::MessageType type,
    Decode decode) {
  std::optional<Packet> packet;
  for (const arv::net::NetworkEvent& event : events) {
    const auto decoded = arv::protocol::deserialize_envelope(event.payload);
    if (!decoded || decoded.value().envelope.message_type != type) {
      continue;
    }
    auto parsed = decode(decoded.value().payload);
    if (parsed) {
      packet = std::move(parsed.value());
    }
  }
  EXPECT_TRUE(packet.has_value());
  return std::move(packet.value());
}

void send_sync_required_hello(
    arv::net::ITransport& server_transport,
    arv::net::ConnectionID connection_id) {
  arv::protocol::ClientboundHello server_hello;
  server_hello.accepted = true;
  server_hello.required_followup_sync = true;
  send_to_client(
      server_transport,
      connection_id,
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(server_hello));
}

std::vector<arv::u8> clientbound_packet(
    arv::protocol::MessageType message_type,
    std::vector<arv::u8> payload) {
  return arv::protocol::serialize_envelope(
      {arv::protocol::CurrentProtocolVersion, message_type, 0, 0, 0, 0},
      payload);
}

class ScriptedClientTransport final : public arv::net::ITransport {
public:
  arv::net::NetworkResult start_listening(const arv::net::ListenAddress&) override {
    return arv::net::NetworkResult::failure({"client test transport cannot listen"});
  }

  arv::net::ConnectResult connect(const arv::net::NetworkAddress&) override {
    state_ = arv::net::ConnectionState::TransportConnected;
    return arv::net::ConnectResult::success(connection_id);
  }

  arv::net::NetworkResult send(arv::net::TransportMessage message) override {
    sent.push_back(std::move(message));
    return arv::net::NetworkResult::success();
  }

  arv::net::NetworkResult close_connection(
      arv::net::ConnectionID,
      std::string_view reason = {}) override {
    state_ = arv::net::ConnectionState::Disconnected;
    close_reasons.push_back(std::string{reason});
    return arv::net::NetworkResult::success();
  }

  std::vector<arv::net::NetworkEvent> poll_events() override {
    std::vector<arv::net::NetworkEvent> output;
    while (!events.empty()) {
      output.push_back(std::move(events.front()));
      events.pop_front();
    }
    return output;
  }

  arv::net::ConnectionState state(arv::net::ConnectionID) const override {
    return state_;
  }

  void shutdown() override {
    state_ = arv::net::ConnectionState::Disconnected;
    events.clear();
  }

  void push_event(arv::net::NetworkEvent event) {
    events.push_back(std::move(event));
  }

  void push_message(
      arv::protocol::MessageType message_type,
      std::vector<arv::u8> payload) {
    events.push_back({
        arv::net::NetworkEventType::MessageReceived,
        connection_id,
        arv::protocol::ReliabilityClass::ReliableOrdered,
        clientbound_packet(message_type, std::move(payload)),
        {},
        {},
        message_type,
        arv::protocol::NetworkLane::Session});
  }

  arv::net::ConnectionID connection_id{arv::net::ConnectionID{77}};
  arv::net::ConnectionState state_{arv::net::ConnectionState::Disconnected};
  std::deque<arv::net::NetworkEvent> events{};
  std::vector<arv::net::TransportMessage> sent{};
  std::vector<std::string> close_reasons{};
};

} // namespace

TEST(ClientSessionTests, CompletesAuthPackDownloadRegistrySyncAndWorldJoin) {
  ScopedTempDir temp("arvane_client_session_pack_");
  const std::string registry_json = "{\"format_version\":1,\"blocks\":[]}";
  const std::filesystem::path packs = temp.path / "packs";
  write_pack_manifest(packs, "sync_pack", "sync_pack");
  write_text(packs / "sync_pack/data/sync_pack/blocks.json", "{\"blocks\":[]}\n");
  write_text(packs / "sync_pack/assets/sync_pack/textures/block/test.png", "png");
  auto pack_manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(pack_manager) << pack_manager.error().describe();
  auto bundle = arv::asset::build_client_pack_bundle(
      pack_manager.value(),
      registry_json);
  ASSERT_TRUE(bundle) << bundle.error().message;

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));

  arv::client::ClientSessionIdentity identity =
      arv::client::make_ephemeral_client_session_identity("WebTester");
  identity.arvane_home = temp.path / "client_home";
  arv::client::ClientSession session(
      std::move(transports.client),
      std::move(identity));
  const auto connection_id = session.connect({"in-process", 0});
  ASSERT_TRUE(connection_id);
  (void)server_transport->poll_events();

  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::ProtocolHandshake);
  std::vector<arv::net::NetworkEvent> messages = poll_messages(*server_transport);
  auto hello = decode_single_serverbound<arv::protocol::ServerboundHello>(
      messages,
      arv::protocol::MessageType::ServerboundHello,
      arv::protocol::deserialize_serverbound_hello);
  EXPECT_EQ(hello.requested_username_or_profile_name, "WebTester");
  ASSERT_TRUE(hello.auth_begin.has_value());

  arv::protocol::ClientboundAuthChallenge challenge;
  challenge.session_id = arv::SessionID{7};
  challenge.expires_server_tick = 99;
  challenge.nonce.fill(std::byte{0x2A});
  challenge.server_instance_id.fill(std::byte{0x11});
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundAuthChallenge,
      arv::protocol::serialize_auth_challenge(challenge));
  session.pump();

  messages = poll_messages(*server_transport);
  auto auth_response = decode_single_serverbound<arv::protocol::ServerboundAuthResponse>(
      messages,
      arv::protocol::MessageType::ServerboundAuthResponse,
      arv::protocol::deserialize_auth_response);
  const std::vector<arv::u8> signed_payload = arv::protocol::build_auth_signed_payload({
      arv::protocol::CurrentProtocolVersion,
      challenge.server_instance_id,
      challenge.session_id,
      session.identity().profile_id,
      session.identity().key_pair.public_key,
      challenge.nonce,
      challenge.expires_server_tick});
  ASSERT_TRUE(arv::crypto::verify_ed25519(
      std::span<const std::byte>{
          reinterpret_cast<const std::byte*>(signed_payload.data()),
          signed_payload.size()},
      auth_response.signature,
      session.identity().key_pair.public_key));

  arv::protocol::ClientboundHello server_hello;
  server_hello.accepted = true;
  server_hello.assigned_session_id = arv::SessionID{7};
  server_hello.assigned_player_id = arv::PlayerID{3};
  server_hello.initial_world_id = arv::WorldID{1};
  server_hello.required_followup_sync = true;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(server_hello));
  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::RegistrySync);

  arv::protocol::ClientboundPackManifest manifest;
  manifest.registry_hash = bundle.value().registry_hash;
  manifest.max_bundle_size = static_cast<arv::u64>(bundle.value().apack_bytes.size());
  manifest.total_download_size = static_cast<arv::u64>(bundle.value().apack_bytes.size());
  manifest.required_bundles.push_back(descriptor_for(bundle.value()));
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPackManifest,
      arv::protocol::serialize_clientbound_pack_manifest(manifest));
  session.pump();
  messages = poll_messages(*server_transport);
  auto status = decode_single_serverbound<arv::protocol::ServerboundPackSyncStatus>(
      messages,
      arv::protocol::MessageType::ServerboundPackSyncStatus,
      arv::protocol::deserialize_serverbound_pack_sync_status);
  EXPECT_EQ(status.status, arv::protocol::PackSyncStatus::NeedsDownload);
  ASSERT_EQ(status.needed_bundle_hashes.size(), 1U);
  EXPECT_EQ(status.needed_bundle_hashes.front(), bundle.value().bundle_hash);
  auto request = decode_single_serverbound<arv::protocol::ServerboundPackRequest>(
      messages,
      arv::protocol::MessageType::ServerboundPackRequest,
      arv::protocol::deserialize_serverbound_pack_request);
  EXPECT_EQ(request.bundle_hash, bundle.value().bundle_hash);

  arv::protocol::ClientboundPackDataChunk chunk;
  chunk.bundle_hash = bundle.value().bundle_hash;
  chunk.chunk_index = 0;
  chunk.chunk_count = 1;
  chunk.byte_offset = 0;
  chunk.bytes = bundle.value().apack_bytes;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPackDataChunk,
      arv::protocol::serialize_clientbound_pack_data_chunk(chunk));
  session.pump();
  messages = poll_messages(*server_transport);
  status = decode_single_serverbound<arv::protocol::ServerboundPackSyncStatus>(
      messages,
      arv::protocol::MessageType::ServerboundPackSyncStatus,
      arv::protocol::deserialize_serverbound_pack_sync_status);
  EXPECT_EQ(status.status, arv::protocol::PackSyncStatus::Loaded);
  ASSERT_EQ(status.already_cached_bundle_hashes.size(), 1U);
  EXPECT_EQ(status.already_cached_bundle_hashes.front(), bundle.value().bundle_hash);
  EXPECT_TRUE(arv::asset::client_pack_cache_contains(
      temp.path / "client_home" / "cache" / "server-packs",
      bundle.value().bundle_hash));
  ASSERT_EQ(session.multiplayer_pack_cache_paths().size(), 1U);
  EXPECT_EQ(
      session.multiplayer_pack_cache_paths().front(),
      temp.path / "client_home" / "cache" / "server-packs" / bundle.value().bundle_hash);

  arv::protocol::ClientboundRegistrySnapshot snapshot;
  snapshot.registry_hash = bundle.value().registry_hash;
  snapshot.registry_snapshot_payload = registry_json;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundRegistrySnapshot,
      arv::protocol::serialize_clientbound_registry_snapshot(snapshot));
  session.pump();
  messages = poll_messages(*server_transport);
  auto ready = decode_single_serverbound<arv::protocol::ServerboundPackReady>(
      messages,
      arv::protocol::MessageType::ServerboundPackReady,
      arv::protocol::deserialize_serverbound_pack_ready);
  EXPECT_EQ(ready.registry_hash, bundle.value().registry_hash);
  ASSERT_EQ(ready.loaded_bundle_hashes.size(), 1U);
  EXPECT_EQ(ready.loaded_bundle_hashes.front(), bundle.value().bundle_hash);
  EXPECT_EQ(session.registry_snapshot_json(), registry_json);
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::WorldJoin);

  arv::protocol::ClientboundWorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{1};
  join.player_id = arv::PlayerID{3};
  join.local_entity_id = arv::EntityNetID{9};
  join.spawn_position = {4.0F, 5.0F, 6.0F};
  join.initial_tick = 12;
  join.tick_rate = arv::DefaultBaseTps;
  join.effective_view_distance = 4;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));
  session.pump();

  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::InGame);
  ASSERT_TRUE(session.world_join().has_value());
  EXPECT_EQ(session.world_join()->world_id, arv::WorldID{1});
  EXPECT_EQ(session.world_join()->local_entity_id, arv::EntityNetID{9});
  messages = poll_messages(*server_transport);
  auto interest = decode_single_serverbound<arv::protocol::ServerboundChunkInterestUpdate>(
      messages,
      arv::protocol::MessageType::ServerboundChunkInterestUpdate,
      arv::protocol::deserialize_serverbound_chunk_interest_update);
  EXPECT_EQ(interest.world_id, arv::WorldID{1});
  EXPECT_EQ(interest.center_chunk, (arv::ChunkSectionCoord{{0, 0, 0}}));
  EXPECT_EQ(interest.client_view_distance, 4);
  EXPECT_FALSE(interest.previous_center_chunk.has_value());
  EXPECT_EQ(interest.sequence_id, 1U);
  ASSERT_FALSE(messages.empty());
  EXPECT_EQ(messages.front().lane, arv::protocol::NetworkLane::Gameplay);

  arv::protocol::ServerboundPlayerMove outgoing_move;
  outgoing_move.client_tick = 13;
  outgoing_move.position = {4.25F, 5.0F, 6.5F};
  outgoing_move.velocity = {0.5F, 0.0F, 1.0F};
  outgoing_move.yaw_radians = 0.25F;
  outgoing_move.pitch_radians = -0.125F;
  outgoing_move.flags = arv::protocol::PlayerMoveFlags::OnGround |
                        arv::protocol::PlayerMoveFlags::Sprinting;
  outgoing_move.action_flags = arv::protocol::PlayerActionFlags::PrimaryPressed;
  outgoing_move.selected_hotbar_slot = 2;
  ASSERT_TRUE(session.submit_player_move(outgoing_move));
  messages = poll_messages(*server_transport);
  auto player_move = decode_single_serverbound<arv::protocol::ServerboundPlayerMove>(
      messages,
      arv::protocol::MessageType::ServerboundPlayerMove,
      arv::protocol::deserialize_serverbound_player_move);
  ASSERT_FALSE(messages.empty());
  EXPECT_EQ(messages.front().reliability, arv::protocol::ReliabilityClass::UnreliableLatestWins);
  EXPECT_EQ(messages.front().lane, arv::protocol::NetworkLane::Movement);
  EXPECT_EQ(player_move.sequence, 1U);
  EXPECT_EQ(player_move.client_tick, 13U);
  EXPECT_EQ(player_move.position, (arv::Vec3f{4.25F, 5.0F, 6.5F}));
  EXPECT_EQ(player_move.velocity, (arv::Vec3f{0.5F, 0.0F, 1.0F}));
  EXPECT_FLOAT_EQ(player_move.yaw_radians, 0.25F);
  EXPECT_FLOAT_EQ(player_move.pitch_radians, -0.125F);
  EXPECT_EQ(player_move.selected_hotbar_slot, 2U);
  EXPECT_TRUE(arv::protocol::has_move_flag(
      player_move.flags,
      arv::protocol::PlayerMoveFlags::Sprinting));
  EXPECT_TRUE(arv::protocol::has_action_flag(
      player_move.action_flags,
      arv::protocol::PlayerActionFlags::PrimaryPressed));

  arv::protocol::ClientboundPlayerMoveCorrection correction;
  correction.rejected_sequence = player_move.sequence;
  correction.position = {4.0F, 5.0F, 6.0F};
  correction.velocity = {};
  correction.yaw_radians = 0.5F;
  correction.pitch_radians = 0.125F;
  correction.flags = arv::protocol::PlayerMoveFlags::OnGround;
  correction.reason = arv::protocol::PlayerMoveCorrectionReason::TooFar;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPlayerMoveCorrection,
      arv::protocol::serialize_clientbound_player_move_correction(correction));
  session.pump();
  std::optional<arv::protocol::ClientboundPlayerMoveCorrection> received_correction =
      session.take_player_move_correction();
  ASSERT_TRUE(received_correction.has_value());
  EXPECT_EQ(received_correction->rejected_sequence, player_move.sequence);
  EXPECT_EQ(received_correction->position, correction.position);
  EXPECT_EQ(received_correction->velocity, correction.velocity);
  EXPECT_FLOAT_EQ(received_correction->yaw_radians, correction.yaw_radians);
  EXPECT_FLOAT_EQ(received_correction->pitch_radians, correction.pitch_radians);
  EXPECT_EQ(received_correction->flags, correction.flags);
  EXPECT_EQ(received_correction->reason, correction.reason);
  EXPECT_FALSE(session.take_player_move_correction().has_value());

  arv::protocol::ClientboundPlayerVelocity velocity;
  velocity.server_tick = 14;
  velocity.player_id = arv::PlayerID{3};
  velocity.velocity = {1.0F, 2.0F, 3.0F};
  velocity.knockback_control_suppression_ticks = 4;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPlayerVelocity,
      arv::protocol::serialize_clientbound_player_velocity(velocity));
  session.pump();
  std::optional<arv::protocol::ClientboundPlayerVelocity> received_velocity =
      session.take_player_velocity();
  ASSERT_TRUE(received_velocity.has_value());
  EXPECT_EQ(received_velocity->player_id, velocity.player_id);
  EXPECT_EQ(received_velocity->velocity, velocity.velocity);
  EXPECT_FALSE(session.take_player_velocity().has_value());

  const arv::protocol::ClientboundSetMovementHz movement_settings{30.0F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundSetMovementHz,
      arv::protocol::serialize_clientbound_set_movement_hz(movement_settings));
  session.pump();
  std::optional<arv::protocol::ClientboundSetMovementHz> received_settings =
      session.take_movement_settings();
  ASSERT_TRUE(received_settings.has_value());
  EXPECT_FLOAT_EQ(received_settings->movement_hz, 30.0F);
  EXPECT_FALSE(session.take_movement_settings().has_value());

  arv::protocol::ClientboundPlayerPermissions permissions;
  permissions.game_mode_id = "creative";
  permissions.core = arv::player::survival_core_permissions();
  permissions.core.can_fly = true;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPlayerPermissions,
      arv::protocol::serialize_clientbound_player_permissions(permissions));
  session.pump();
  std::optional<arv::protocol::ClientboundPlayerPermissions> received_permissions =
      session.take_player_permissions();
  ASSERT_TRUE(received_permissions.has_value());
  EXPECT_EQ(received_permissions->game_mode_id, "creative");
  EXPECT_TRUE(received_permissions->core.can_fly);
  EXPECT_FALSE(session.take_player_permissions().has_value());

  const arv::protocol::ClientboundPlayerMovementTuning movement_tuning{9.5F, 6.25F};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPlayerMovementTuning,
      arv::protocol::serialize_clientbound_player_movement_tuning(movement_tuning));
  session.pump();
  std::optional<arv::protocol::ClientboundPlayerMovementTuning> received_tuning =
      session.take_player_movement_tuning();
  ASSERT_TRUE(received_tuning.has_value());
  EXPECT_FLOAT_EQ(received_tuning->fly_speed, 9.5F);
  EXPECT_FLOAT_EQ(received_tuning->fly_vertical_speed, 6.25F);
  EXPECT_FALSE(session.take_player_movement_tuning().has_value());

  arv::protocol::ClientboundChunkSectionData chunk_data;
  chunk_data.world_id = arv::WorldID{1};
  chunk_data.coord = arv::ChunkSectionCoord{{0, 0, 0}};
  chunk_data.chunk_version = 10;
  chunk_data.chunk_content_version = 11;
  chunk_data.chunk_light_version = 12;
  chunk_data.has_lighting = true;
  chunk_data.lighting_valid = true;
  chunk_data.blocks.fill(arv::voxel::AirBlockVariantID);
  chunk_data.lights.emplace();
  chunk_data.lights->fill(arv::voxel::pack_light(1, 2, 3, 4));
  chunk_data.blocks.front() = arv::voxel::BlockVariantID{7};
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundChunkSectionData,
      arv::protocol::serialize_clientbound_chunk_section_data(chunk_data));
  session.pump();

  ASSERT_EQ(session.chunk_sections().size(), 1U);
  EXPECT_EQ(session.chunk_sections().front().coord, chunk_data.coord);
  EXPECT_EQ(session.chunk_sections().front().blocks.front(), arv::voxel::BlockVariantID{7});
  ASSERT_TRUE(session.chunk_sections().front().lights.has_value());
  EXPECT_EQ(
      session.chunk_sections().front().lights.value().front(),
      arv::voxel::pack_light(1, 2, 3, 4));

  arv::protocol::ClientboundBlockUpdateBatch block_updates;
  block_updates.world_id = arv::WorldID{1};
  block_updates.server_tick = 200;
  block_updates.updates.push_back({
      arv::BlockPos{{0, 0, 0}},
      arv::voxel::BlockVariantID{8},
      0U,
      200U,
      true,
      arv::voxel::make_fluid_source(arv::voxel::make_water_fluid_definition()),
  });
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundBlockUpdateBatch,
      arv::protocol::serialize_clientbound_block_update_batch(block_updates));
  session.pump();

  ASSERT_EQ(session.chunk_sections().size(), 1U);
  EXPECT_EQ(session.chunk_sections().front().blocks.front(), arv::voxel::BlockVariantID{8});
  EXPECT_TRUE(session.chunk_sections().front().has_fluids);
  ASSERT_TRUE(session.chunk_sections().front().fluids.has_value());
  EXPECT_TRUE(session.chunk_sections().front().fluids.value().front().source());
  EXPECT_GT(session.chunk_sections().front().chunk_content_version, chunk_data.chunk_content_version);

  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundChunkSectionUnload,
      arv::protocol::serialize_clientbound_chunk_section_unload(
          {arv::WorldID{1}, chunk_data.coord}));
  session.pump();
  EXPECT_TRUE(session.chunk_sections().empty());
}

TEST(ClientSessionTests, ReconnectUsesVerifiedPackCacheAndSkipsBundleDownload) {
  ScopedTempDir temp("arvane_client_session_reconnect_");
  const std::string registry_json = "{\"format_version\":1,\"blocks\":[]}";
  const std::filesystem::path packs = temp.path / "packs";
  write_pack_manifest(packs, "sync_pack", "sync_pack");
  write_text(packs / "sync_pack/data/sync_pack/blocks.json", "{\"blocks\":[]}\n");
  write_text(packs / "sync_pack/assets/sync_pack/textures/block/test.png", "png");
  auto pack_manager = arv::asset::PackManager::load_from_packs_root(packs);
  ASSERT_TRUE(pack_manager) << pack_manager.error().describe();
  auto bundle = arv::asset::build_client_pack_bundle(
      pack_manager.value(),
      registry_json);
  ASSERT_TRUE(bundle) << bundle.error().message;

  arv::client::ClientSessionIdentity identity =
      arv::client::make_ephemeral_client_session_identity("WebCacheTester");
  identity.arvane_home = temp.path / "client_home";
  const std::filesystem::path cache_root =
      identity.arvane_home / "cache" / "server-packs";
  const std::filesystem::path expected_cache_path =
      cache_root / bundle.value().bundle_hash;

  {
    auto transports = arv::net::make_in_process_transport_pair();
    std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
    ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
    arv::client::ClientSession first_session(
        std::move(transports.client),
        identity);
    const auto connection_id = first_session.connect({"in-process", 0});
    ASSERT_TRUE(connection_id);
    (void)server_transport->poll_events();
    first_session.pump();
    (void)poll_messages(*server_transport);

    send_sync_required_hello(*server_transport, connection_id.value());
    first_session.pump();

    arv::protocol::ClientboundPackManifest manifest;
    manifest.registry_hash = bundle.value().registry_hash;
    manifest.max_bundle_size = static_cast<arv::u64>(bundle.value().apack_bytes.size());
    manifest.total_download_size = static_cast<arv::u64>(bundle.value().apack_bytes.size());
    manifest.required_bundles.push_back(descriptor_for(bundle.value()));
    send_to_client(
        *server_transport,
        connection_id.value(),
        arv::protocol::MessageType::ClientboundPackManifest,
        arv::protocol::serialize_clientbound_pack_manifest(manifest));
    first_session.pump();
    std::vector<arv::net::NetworkEvent> messages = poll_messages(*server_transport);
    const auto first_status = decode_single_serverbound<arv::protocol::ServerboundPackSyncStatus>(
        messages,
        arv::protocol::MessageType::ServerboundPackSyncStatus,
        arv::protocol::deserialize_serverbound_pack_sync_status);
    EXPECT_EQ(first_status.status, arv::protocol::PackSyncStatus::NeedsDownload);
    EXPECT_EQ(
        message_type_count(messages, arv::protocol::MessageType::ServerboundPackRequest),
        1U);

    arv::protocol::ClientboundPackDataChunk chunk;
    chunk.bundle_hash = bundle.value().bundle_hash;
    chunk.chunk_index = 0;
    chunk.chunk_count = 1;
    chunk.byte_offset = 0;
    chunk.bytes = bundle.value().apack_bytes;
    send_to_client(
        *server_transport,
        connection_id.value(),
        arv::protocol::MessageType::ClientboundPackDataChunk,
        arv::protocol::serialize_clientbound_pack_data_chunk(chunk));
    first_session.pump();
    messages = poll_messages(*server_transport);
    const auto loaded_status = decode_single_serverbound<arv::protocol::ServerboundPackSyncStatus>(
        messages,
        arv::protocol::MessageType::ServerboundPackSyncStatus,
        arv::protocol::deserialize_serverbound_pack_sync_status);
    EXPECT_EQ(loaded_status.status, arv::protocol::PackSyncStatus::Loaded);
    ASSERT_EQ(first_session.multiplayer_pack_cache_paths().size(), 1U);
    EXPECT_EQ(first_session.multiplayer_pack_cache_paths().front(), expected_cache_path);
  }

  ASSERT_TRUE(arv::asset::client_pack_cache_contains(cache_root, bundle.value().bundle_hash));

  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));
  arv::client::ClientSession second_session(
      std::move(transports.client),
      identity);
  const auto connection_id = second_session.connect({"in-process", 0});
  ASSERT_TRUE(connection_id);
  (void)server_transport->poll_events();
  second_session.pump();
  (void)poll_messages(*server_transport);

  send_sync_required_hello(*server_transport, connection_id.value());
  second_session.pump();

  arv::protocol::ClientboundPackManifest manifest;
  manifest.registry_hash = bundle.value().registry_hash;
  manifest.max_bundle_size = static_cast<arv::u64>(bundle.value().apack_bytes.size());
  manifest.total_download_size = static_cast<arv::u64>(bundle.value().apack_bytes.size());
  manifest.required_bundles.push_back(descriptor_for(bundle.value()));
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPackManifest,
      arv::protocol::serialize_clientbound_pack_manifest(manifest));
  second_session.pump();
  std::vector<arv::net::NetworkEvent> messages = poll_messages(*server_transport);
  const auto cached_status = decode_single_serverbound<arv::protocol::ServerboundPackSyncStatus>(
      messages,
      arv::protocol::MessageType::ServerboundPackSyncStatus,
      arv::protocol::deserialize_serverbound_pack_sync_status);
  EXPECT_EQ(cached_status.status, arv::protocol::PackSyncStatus::AlreadyCached);
  ASSERT_EQ(cached_status.already_cached_bundle_hashes.size(), 1U);
  EXPECT_EQ(cached_status.already_cached_bundle_hashes.front(), bundle.value().bundle_hash);
  EXPECT_TRUE(cached_status.needed_bundle_hashes.empty());
  EXPECT_EQ(
      message_type_count(messages, arv::protocol::MessageType::ServerboundPackRequest),
      0U);
  ASSERT_EQ(second_session.multiplayer_pack_cache_paths().size(), 1U);
  EXPECT_EQ(second_session.multiplayer_pack_cache_paths().front(), expected_cache_path);

  arv::protocol::ClientboundRegistrySnapshot snapshot;
  snapshot.registry_hash = bundle.value().registry_hash;
  snapshot.registry_snapshot_payload = registry_json;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundRegistrySnapshot,
      arv::protocol::serialize_clientbound_registry_snapshot(snapshot));
  second_session.pump();
  messages = poll_messages(*server_transport);
  const auto ready = decode_single_serverbound<arv::protocol::ServerboundPackReady>(
      messages,
      arv::protocol::MessageType::ServerboundPackReady,
      arv::protocol::deserialize_serverbound_pack_ready);
  EXPECT_EQ(ready.registry_hash, bundle.value().registry_hash);
  ASSERT_EQ(ready.loaded_bundle_hashes.size(), 1U);
  EXPECT_EQ(ready.loaded_bundle_hashes.front(), bundle.value().bundle_hash);
  EXPECT_EQ(second_session.registry_snapshot_json(), registry_json);
  EXPECT_EQ(second_session.connection_state(), arv::net::ConnectionState::WorldJoin);
}

TEST(ClientSessionTests, OutOfStateWorldJoinDisconnectsBeforeEnteringGame) {
  auto transport = std::make_unique<ScriptedClientTransport>();
  ScriptedClientTransport* scripted = transport.get();
  arv::client::ClientSession session(std::move(transport));
  const auto connection_id = session.connect({"in-process", 0});
  ASSERT_TRUE(connection_id);
  scripted->push_event({
      arv::net::NetworkEventType::ConnectionAccepted,
      connection_id.value()});
  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::ProtocolHandshake);

  arv::protocol::ClientboundWorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{1};
  join.player_id = arv::PlayerID{2};
  join.local_entity_id = arv::EntityNetID{3};
  join.spawn_position = {1.0F, 2.0F, 3.0F};
  scripted->push_message(
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));
  session.pump();

  EXPECT_EQ(session.connection_state(), arv::net::ConnectionState::Disconnected);
  ASSERT_TRUE(session.disconnect_reason().has_value());
  EXPECT_NE(session.disconnect_reason()->find("packet 0x"), std::string::npos);
  EXPECT_NE(session.disconnect_reason()->find("session state"), std::string::npos);
  EXPECT_FALSE(session.world_join().has_value());
  ASSERT_EQ(scripted->close_reasons.size(), 1U);
  EXPECT_NE(scripted->close_reasons.front().find("packet 0x"), std::string::npos);
  EXPECT_NE(scripted->close_reasons.front().find("session state"), std::string::npos);
}

TEST(ClientSessionTests, SpaceWorldJoinDoesNotSendInitialChunkInterest) {
  auto transport = std::make_unique<ScriptedClientTransport>();
  ScriptedClientTransport* scripted = transport.get();
  arv::client::ClientSession session(std::move(transport));
  const auto connection_id = session.connect({"in-process", 0});
  ASSERT_TRUE(connection_id);
  scripted->push_event({
      arv::net::NetworkEventType::ConnectionAccepted,
      connection_id.value()});
  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::ProtocolHandshake);

  arv::protocol::ClientboundHello server_hello;
  server_hello.accepted = true;
  server_hello.required_followup_sync = false;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(server_hello));
  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::WorldJoin);
  scripted->sent.clear();

  arv::protocol::ClientboundWorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{7};
  join.player_id = arv::PlayerID{2};
  join.local_entity_id = arv::EntityNetID{3};
  join.spawn_position = {32.0F, 0.0F, -32.0F};
  join.tick_rate = arv::DefaultBaseTps;
  join.effective_view_distance = 4;
  join.environment_sky = "space";
  join.space_environment = true;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));
  session.pump();

  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::InGame);
  ASSERT_TRUE(session.world_join().has_value());
  EXPECT_TRUE(session.world_join()->space_environment);
  EXPECT_EQ(
      transport_message_type_count(
          scripted->sent,
          arv::protocol::MessageType::ServerboundChunkInterestUpdate),
      0U);
}

TEST(ClientSessionTests, StaleMessageAfterConnectionCloseDoesNotResurrectWorldState) {
  auto transport = std::make_unique<ScriptedClientTransport>();
  ScriptedClientTransport* scripted = transport.get();
  arv::client::ClientSession session(std::move(transport));
  const auto connection_id = session.connect({"in-process", 0});
  ASSERT_TRUE(connection_id);

  arv::protocol::ClientboundWorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{1};
  join.player_id = arv::PlayerID{2};
  join.local_entity_id = arv::EntityNetID{3};
  join.spawn_position = {1.0F, 2.0F, 3.0F};
  scripted->push_event({
      arv::net::NetworkEventType::ConnectionClosed,
      connection_id.value(),
      arv::protocol::ReliabilityClass::ReliableOrdered,
      {},
      "network closed"});
  scripted->push_message(
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));
  session.pump();

  EXPECT_EQ(session.connection_state(), arv::net::ConnectionState::Disconnected);
  ASSERT_TRUE(session.disconnect_reason().has_value());
  EXPECT_EQ(*session.disconnect_reason(), "network closed");
  EXPECT_FALSE(session.world_join().has_value());
  EXPECT_TRUE(session.chunk_sections().empty());
  EXPECT_TRUE(scripted->close_reasons.empty());
}

TEST(ClientSessionTests, TracksEntityMirrorsAndRejectsMalformedEntityPackets) {
  auto transport = std::make_unique<ScriptedClientTransport>();
  ScriptedClientTransport* scripted = transport.get();
  arv::client::ClientSession session(std::move(transport));
  const auto connection_id = session.connect({"in-process", 0});
  ASSERT_TRUE(connection_id);
  scripted->push_event({
      arv::net::NetworkEventType::ConnectionAccepted,
      connection_id.value()});
  session.pump();

  arv::protocol::ClientboundHello server_hello;
  server_hello.accepted = true;
  server_hello.required_followup_sync = false;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(server_hello));
  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::WorldJoin);

  {
    arv::protocol::ClientboundEntitySpawn early_spawn;
    early_spawn.world_id = arv::WorldID{4};
    early_spawn.entity_id = arv::EntityNetID{11};
    early_spawn.entity_type = arv::EntityTypeID{6};
    early_spawn.entity_type_name = "arvane:test_entity";
    early_spawn.position = {10.0F, 11.0F, 12.0F};
    early_spawn.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
    early_spawn.owning_player = arv::PlayerID{2};
    scripted->push_message(
        arv::protocol::MessageType::ClientboundEntitySpawn,
        arv::protocol::serialize_clientbound_entity_spawn(early_spawn));
    session.pump();
  }

  ASSERT_FALSE(session.world_join().has_value());
  ASSERT_EQ(session.entity_mirrors().size(), 1U);
  EXPECT_EQ(session.entity_mirrors().front().entity_id, arv::EntityNetID{11});
  ASSERT_TRUE(session.entity_mirrors().front().owning_player.has_value());
  EXPECT_EQ(session.entity_mirrors().front().owning_player.value(), arv::PlayerID{2});

  arv::protocol::ClientboundWorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{4};
  join.player_id = arv::PlayerID{2};
  join.local_entity_id = arv::EntityNetID{3};
  join.spawn_position = {1.0F, 2.0F, 3.0F};
  join.tick_rate = arv::DefaultBaseTps;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));
  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::InGame);

  arv::protocol::ClientboundEntitySpawn spawn;
  spawn.world_id = arv::WorldID{4};
  spawn.entity_id = arv::EntityNetID{11};
  spawn.entity_type = arv::EntityTypeID{6};
  spawn.entity_type_name = "arvane:test_entity";
  spawn.position = {10.0F, 11.0F, 12.0F};
  spawn.velocity = {0.25F, 0.5F, 0.75F};
  spawn.rotation = {0.0F, 0.0F, 0.0F, 1.0F};
  spawn.spawn_flags = 7U;
  spawn.owning_player = arv::PlayerID{2};
  scripted->push_message(
      arv::protocol::MessageType::ClientboundEntitySpawn,
      arv::protocol::serialize_clientbound_entity_spawn(spawn));
  session.pump();

  ASSERT_EQ(session.entity_mirrors().size(), 1U);
  EXPECT_EQ(session.entity_mirrors().front().entity_id, arv::EntityNetID{11});
  EXPECT_EQ(session.entity_mirrors().front().entity_type_name, "arvane:test_entity");
  EXPECT_EQ(session.entity_mirrors().front().position, (arv::Vec3f{10.0F, 11.0F, 12.0F}));
  ASSERT_TRUE(session.entity_mirrors().front().owning_player.has_value());
  EXPECT_EQ(session.entity_mirrors().front().owning_player.value(), arv::PlayerID{2});

  arv::protocol::ClientboundEntitySnapshot snapshot;
  snapshot.world_id = arv::WorldID{4};
  snapshot.server_tick = 20U;
  arv::protocol::EntitySnapshotEntry entry;
  entry.entity_id = arv::EntityNetID{11};
  entry.position = {13.0F, 14.0F, 15.0F};
  entry.velocity = {1.0F, 0.0F, -1.0F};
  entry.rotation = {0.0F, 0.25F, 0.0F, 0.9682458F};
  entry.state_flags = arv::protocol::EntityStateFlagHasHealth;
  entry.animations.push_back({"arvane:animation/player/walk", arv::Seconds{1.25}});
  entry.animations.push_back({"arvane:animation/player/swing", arv::Seconds{0.5}});
  entry.head_rotation_degrees = {5.0F, 10.0F, 0.0F};
  entry.health_current = 8.0F;
  entry.health_max = 10.0F;
  snapshot.entities.push_back(entry);
  scripted->push_message(
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(snapshot));
  session.pump();

  ASSERT_EQ(session.entity_mirrors().size(), 1U);
  EXPECT_EQ(session.entity_mirrors().front().position, (arv::Vec3f{13.0F, 14.0F, 15.0F}));
  EXPECT_EQ(session.entity_mirrors().front().velocity, (arv::Vec3f{1.0F, 0.0F, -1.0F}));
  EXPECT_EQ(session.entity_mirrors().front().state_flags, arv::protocol::EntityStateFlagHasHealth);
  ASSERT_EQ(session.entity_mirrors().front().animations.size(), 2U);
  EXPECT_EQ(session.entity_mirrors().front().animations[0].name, "arvane:animation/player/walk");
  EXPECT_FLOAT_EQ(session.entity_mirrors().front().animations[0].time_seconds, arv::Seconds{1.25F});
  EXPECT_EQ(session.entity_mirrors().front().animations[1].name, "arvane:animation/player/swing");
  EXPECT_FLOAT_EQ(session.entity_mirrors().front().animations[1].time_seconds, arv::Seconds{0.5F});
  EXPECT_EQ(session.entity_mirrors().front().head_rotation_degrees, (arv::Vec3f{5.0F, 10.0F, 0.0F}));
  EXPECT_FLOAT_EQ(session.entity_mirrors().front().health_current, 8.0F);
  EXPECT_FLOAT_EQ(session.entity_mirrors().front().health_max, 10.0F);
  EXPECT_EQ(session.entity_mirrors().front().last_server_tick, 20U);

  snapshot.server_tick = 19U;
  snapshot.entities.front().position = {99.0F, 99.0F, 99.0F};
  scripted->push_message(
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(snapshot));
  session.pump();
  EXPECT_EQ(session.entity_mirrors().front().position, (arv::Vec3f{13.0F, 14.0F, 15.0F}));

  snapshot.world_id = arv::WorldID{5};
  snapshot.server_tick = 21U;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundEntitySnapshot,
      arv::protocol::serialize_clientbound_entity_snapshot(snapshot));
  session.pump();
  EXPECT_EQ(session.entity_mirrors().front().last_server_tick, 20U);

  arv::protocol::ClientboundEntityDespawn wrong_world_despawn{
      arv::WorldID{5},
      arv::EntityNetID{11},
      1U};
  scripted->push_message(
      arv::protocol::MessageType::ClientboundEntityDespawn,
      arv::protocol::serialize_clientbound_entity_despawn(wrong_world_despawn));
  session.pump();
  EXPECT_EQ(session.entity_mirrors().size(), 1U);

  arv::protocol::ClientboundEntityDespawn despawn{
      arv::WorldID{4},
      arv::EntityNetID{11},
      1U};
  scripted->push_message(
      arv::protocol::MessageType::ClientboundEntityDespawn,
      arv::protocol::serialize_clientbound_entity_despawn(despawn));
  session.pump();
  EXPECT_TRUE(session.entity_mirrors().empty());

  scripted->push_message(
      arv::protocol::MessageType::ClientboundEntitySpawn,
      std::vector<arv::u8>{1U, 2U, 3U});
  session.pump();
  EXPECT_EQ(session.connection_state(), arv::net::ConnectionState::Disconnected);
  ASSERT_TRUE(session.disconnect_reason().has_value());
  EXPECT_NE(session.disconnect_reason()->find("invalid entity spawn"), std::string::npos);
}

TEST(ClientSessionTests, TracksInventoryAndDroppedItemsForLightweightClient) {
  auto transport = std::make_unique<ScriptedClientTransport>();
  ScriptedClientTransport* scripted = transport.get();
  arv::client::ClientSession session(std::move(transport));
  const auto connection_id = session.connect({"in-process", 0});
  ASSERT_TRUE(connection_id);
  scripted->push_event({
      arv::net::NetworkEventType::ConnectionAccepted,
      connection_id.value()});
  session.pump();

  arv::protocol::ClientboundHello server_hello;
  server_hello.accepted = true;
  server_hello.required_followup_sync = false;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(server_hello));
  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::WorldJoin);

  arv::protocol::ClientboundWorldJoinResult join;
  join.accepted = true;
  join.world_id = arv::WorldID{7};
  join.player_id = arv::PlayerID{2};
  join.local_entity_id = arv::EntityNetID{3};
  join.spawn_position = {1.0F, 2.0F, 3.0F};
  join.tick_rate = arv::DefaultBaseTps;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundWorldJoinResult,
      arv::protocol::serialize_clientbound_world_join_result(join));
  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::InGame);

  arv::protocol::ClientboundInventoryUpdate inventory;
  inventory.inventory_id = arv::protocol::PlayerInventoryID;
  inventory.revision = 1U;
  inventory.selected_hotbar_slot = 0U;
  inventory.slots.resize(arv::player::DefaultPlayerInventorySlotCount);
  inventory.slots[0] = arv::player::ItemStack{arv::player::ItemID{5}, 12U};
  inventory.carried_stack = arv::player::ItemStack{arv::player::ItemID{6}, 1U};
  scripted->push_message(
      arv::protocol::MessageType::ClientboundInventoryUpdate,
      arv::protocol::serialize_clientbound_inventory_update(inventory));
  session.pump();

  EXPECT_TRUE(session.has_authoritative_inventory());
  EXPECT_EQ(session.inventory_revision(), 1U);
  ASSERT_EQ(session.inventory().slot_count(), arv::player::DefaultPlayerInventorySlotCount);
  ASSERT_TRUE(session.inventory().slot(0).has_value());
  EXPECT_EQ(session.inventory().slot(0)->item, arv::player::ItemID{5});
  EXPECT_EQ(session.inventory().slot(0)->count, 12U);
  ASSERT_TRUE(session.inventory_cursor_stack().has_value());
  EXPECT_EQ(session.inventory_cursor_stack()->item, arv::player::ItemID{6});

  arv::protocol::ClientboundPlayerBlockBreakProgress break_progress;
  break_progress.server_tick = 15U;
  break_progress.player_id = arv::PlayerID{2};
  break_progress.block_break.active = true;
  break_progress.block_break.block = arv::BlockPos{{4, 5, 6}};
  break_progress.block_break.variant = arv::voxel::BlockVariantID{7};
  break_progress.block_break.progress = 0.45F;
  break_progress.block_break.stage = 4U;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundPlayerBlockBreakProgress,
      arv::protocol::serialize_clientbound_player_block_break_progress(break_progress));
  session.pump();

  EXPECT_TRUE(session.block_break_progress().active);
  EXPECT_EQ(session.block_break_progress().block, (arv::BlockPos{{4, 5, 6}}));
  EXPECT_EQ(session.block_break_progress().variant, arv::voxel::BlockVariantID{7});
  EXPECT_FLOAT_EQ(session.block_break_progress().progress, 0.45F);
  EXPECT_EQ(session.block_break_progress().stage, 4U);

  break_progress.player_id = arv::PlayerID{99};
  break_progress.block_break.block = arv::BlockPos{{9, 9, 9}};
  scripted->push_message(
      arv::protocol::MessageType::ClientboundPlayerBlockBreakProgress,
      arv::protocol::serialize_clientbound_player_block_break_progress(break_progress));
  session.pump();
  EXPECT_EQ(session.block_break_progress().block, (arv::BlockPos{{4, 5, 6}}));

  inventory.slots[0] = arv::player::ItemStack{arv::player::ItemID{9}, 1U};
  scripted->push_message(
      arv::protocol::MessageType::ClientboundInventoryUpdate,
      arv::protocol::serialize_clientbound_inventory_update(inventory));
  session.pump();
  ASSERT_TRUE(session.inventory().slot(0).has_value());
  EXPECT_EQ(session.inventory().slot(0)->item, arv::player::ItemID{5});

  arv::protocol::ClientboundDroppedItemSnapshot dropped;
  dropped.world_id = arv::WorldID{7};
  dropped.server_tick = 12U;
  dropped.drops.push_back({
      arv::EntityNetID{44},
      arv::player::ItemStack{arv::player::ItemID{5}, 3U},
      {4.0F, 5.0F, 6.0F},
      {0.25F, 0.0F, 0.0F}});
  scripted->push_message(
      arv::protocol::MessageType::ClientboundDroppedItemSnapshot,
      arv::protocol::serialize_clientbound_dropped_item_snapshot(dropped));
  session.pump();

  EXPECT_EQ(session.dropped_item_count(), 1U);
  const arv::client::DroppedItemMirror* mirror =
      session.find_dropped_item(arv::EntityNetID{44});
  ASSERT_NE(mirror, nullptr);
  EXPECT_EQ(mirror->world_id, arv::WorldID{7});
  EXPECT_EQ(mirror->stack.item, arv::player::ItemID{5});
  EXPECT_EQ(mirror->stack.count, 3U);
  EXPECT_EQ(mirror->render_position, (arv::Vec3f{4.0F, 5.0F, 6.0F}));

  dropped.world_id = arv::WorldID{8};
  dropped.drops.clear();
  scripted->push_message(
      arv::protocol::MessageType::ClientboundDroppedItemSnapshot,
      arv::protocol::serialize_clientbound_dropped_item_snapshot(dropped));
  session.pump();
  EXPECT_EQ(session.dropped_item_count(), 1U);

  session.disconnect("test reset");
  EXPECT_FALSE(session.has_authoritative_inventory());
  EXPECT_FALSE(session.block_break_progress().active);
  EXPECT_EQ(session.dropped_item_count(), 0U);
}

TEST(ClientSessionTests, AcceptsInventoryUpdateDuringRegistrySync) {
  auto transport = std::make_unique<ScriptedClientTransport>();
  ScriptedClientTransport* scripted = transport.get();
  arv::client::ClientSession session(std::move(transport));
  const auto connection_id = session.connect({"in-process", 0});
  ASSERT_TRUE(connection_id);
  scripted->push_event({
      arv::net::NetworkEventType::ConnectionAccepted,
      connection_id.value()});
  session.pump();

  arv::protocol::ClientboundHello server_hello;
  server_hello.accepted = true;
  server_hello.required_followup_sync = true;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(server_hello));
  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::RegistrySync);

  arv::protocol::ClientboundInventoryUpdate inventory;
  inventory.inventory_id = arv::protocol::PlayerInventoryID;
  inventory.revision = 1U;
  inventory.selected_hotbar_slot = 0U;
  inventory.slots.resize(arv::player::DefaultPlayerInventorySlotCount);
  inventory.slots[0] = arv::player::ItemStack{arv::player::ItemID{5}, 12U};
  scripted->push_message(
      arv::protocol::MessageType::ClientboundInventoryUpdate,
      arv::protocol::serialize_clientbound_inventory_update(inventory));
  session.pump();

  EXPECT_EQ(session.connection_state(), arv::net::ConnectionState::RegistrySync);
  EXPECT_TRUE(scripted->close_reasons.empty());
  EXPECT_TRUE(session.has_authoritative_inventory());
  ASSERT_TRUE(session.inventory().slot(0).has_value());
  EXPECT_EQ(session.inventory().slot(0)->item, arv::player::ItemID{5});
  EXPECT_EQ(session.inventory().slot(0)->count, 12U);
}

TEST(ClientSessionTests, AcceptsBlockBreakProgressDuringRegistrySync) {
  auto transport = std::make_unique<ScriptedClientTransport>();
  ScriptedClientTransport* scripted = transport.get();
  arv::client::ClientSession session(std::move(transport));
  const auto connection_id = session.connect({"in-process", 0});
  ASSERT_TRUE(connection_id);
  scripted->push_event({
      arv::net::NetworkEventType::ConnectionAccepted,
      connection_id.value()});
  session.pump();

  arv::protocol::ClientboundHello server_hello;
  server_hello.accepted = true;
  server_hello.required_followup_sync = true;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(server_hello));
  session.pump();
  ASSERT_EQ(session.connection_state(), arv::net::ConnectionState::RegistrySync);

  arv::protocol::ClientboundPlayerBlockBreakProgress progress;
  progress.server_tick = 1U;
  progress.player_id = arv::PlayerID{2};
  progress.block_break.active = true;
  progress.block_break.block = arv::BlockPos{{1, 2, 3}};
  progress.block_break.variant = arv::voxel::BlockVariantID{4};
  progress.block_break.progress = 0.25F;
  progress.block_break.stage = 2U;
  scripted->push_message(
      arv::protocol::MessageType::ClientboundPlayerBlockBreakProgress,
      arv::protocol::serialize_clientbound_player_block_break_progress(progress));
  session.pump();

  EXPECT_EQ(session.connection_state(), arv::net::ConnectionState::RegistrySync);
  EXPECT_TRUE(scripted->close_reasons.empty());
  EXPECT_TRUE(session.block_break_progress().active);
  EXPECT_EQ(session.block_break_progress().block, (arv::BlockPos{{1, 2, 3}}));
}

TEST(ClientSessionTests, RegistrySnapshotMismatchBlocksWorldJoin) {
  auto transports = arv::net::make_in_process_transport_pair();
  std::unique_ptr<arv::net::ITransport> server_transport = std::move(transports.server);
  ASSERT_TRUE(server_transport->start_listening({"in-process", 0}));

  arv::client::ClientSession session(std::move(transports.client));
  const auto connection_id = session.connect({"in-process", 0});
  ASSERT_TRUE(connection_id);
  (void)server_transport->poll_events();
  session.pump();
  (void)poll_messages(*server_transport);

  arv::protocol::ClientboundHello server_hello;
  server_hello.accepted = true;
  server_hello.required_followup_sync = true;
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundHello,
      arv::protocol::serialize_clientbound_hello(server_hello));
  session.pump();

  arv::protocol::ClientboundPackManifest manifest;
  manifest.registry_hash = std::string(64U, 'a');
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundPackManifest,
      arv::protocol::serialize_clientbound_pack_manifest(manifest));
  session.pump();
  (void)poll_messages(*server_transport);

  arv::protocol::ClientboundRegistrySnapshot snapshot;
  snapshot.registry_hash = std::string(64U, 'b');
  snapshot.registry_snapshot_payload = "{\"format_version\":1,\"blocks\":[]}";
  send_to_client(
      *server_transport,
      connection_id.value(),
      arv::protocol::MessageType::ClientboundRegistrySnapshot,
      arv::protocol::serialize_clientbound_registry_snapshot(snapshot));
  session.pump();

  const std::vector<arv::net::NetworkEvent> messages = poll_messages(*server_transport);
  const auto status = decode_single_serverbound<arv::protocol::ServerboundPackSyncStatus>(
      messages,
      arv::protocol::MessageType::ServerboundPackSyncStatus,
      arv::protocol::deserialize_serverbound_pack_sync_status);
  EXPECT_EQ(status.status, arv::protocol::PackSyncStatus::VerificationFailed);
  EXPECT_EQ(session.connection_state(), arv::net::ConnectionState::Disconnected);
  ASSERT_TRUE(session.disconnect_reason().has_value());
  EXPECT_NE(session.disconnect_reason()->find("registry snapshot"), std::string::npos);
  EXPECT_FALSE(session.world_join().has_value());
}
