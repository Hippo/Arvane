#include "server_runtime_internal.hpp"

#include "arv/core/log.hpp"
#include "arv/crypto/hash.hpp"
#include "arv/physics/player_movement.hpp"
#include "arv/protocol/auth.hpp"
#include "arv/protocol/chat.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/protocol/skins.hpp"
#include "arv/server/skin_runtime.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <utility>

namespace arv::server::detail {
namespace {

void configure_skin_transfer_state(
    SkinTransferSessionState& state,
    const io::SkinConfig& skins) {
  if (state.limiters_initialized) {
    return;
  }
  state.upload_begin_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_begin_attempts_per_minute),
      static_cast<double>(skins.uploads.max_begin_attempts_per_minute) / 60.0};
  state.completed_upload_minute_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_completed_uploads_per_minute),
      static_cast<double>(skins.uploads.max_completed_uploads_per_minute) / 60.0};
  state.completed_upload_hour_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_completed_uploads_per_hour),
      static_cast<double>(skins.uploads.max_completed_uploads_per_hour) / 3600.0};
  state.upload_byte_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_upload_bytes_per_hour),
      static_cast<double>(skins.uploads.max_upload_bytes_per_hour) / 3600.0};
  state.failed_decode_limiter = core::TokenBucket{
      static_cast<double>(skins.uploads.max_failed_decodes_per_minute),
      static_cast<double>(skins.uploads.max_failed_decodes_per_minute) / 60.0};
  state.request_second_limiter = core::TokenBucket{
      static_cast<double>(skins.downloads.max_requests_per_second),
      static_cast<double>(skins.downloads.max_requests_per_second)};
  state.request_minute_limiter = core::TokenBucket{
      static_cast<double>(skins.downloads.max_requests_per_minute),
      static_cast<double>(skins.downloads.max_requests_per_minute) / 60.0};
  state.download_byte_limiter = core::TokenBucket{
      static_cast<double>(skins.downloads.max_bytes_per_minute),
      static_cast<double>(skins.downloads.max_bytes_per_minute) / 60.0};
  state.limiters_initialized = true;
}

[[nodiscard]] TickIndex seconds_to_ticks(u32 seconds, TickRate tick_rate) noexcept {
  const TickIndex ticks_per_second = std::max<TickIndex>(1U, tick_rate);
  if (seconds > std::numeric_limits<TickIndex>::max() / ticks_per_second) {
    return std::numeric_limits<TickIndex>::max();
  }
  return std::max<TickIndex>(1U, static_cast<TickIndex>(seconds) * ticks_per_second);
}

[[nodiscard]] bool elapsed_ticks_exceeds(
    TickIndex now,
    TickIndex then,
    TickIndex limit) noexcept {
  return now < then || now - then > limit;
}

[[nodiscard]] bool skin_upload_timed_out(
    const ActiveSkinUpload& upload,
    TickIndex tick_index,
    const io::SkinConfig& skins,
    TickRate tick_rate) noexcept {
  return elapsed_ticks_exceeds(
             tick_index,
             upload.started_tick,
             seconds_to_ticks(skins.uploads.timeout_seconds, tick_rate)) ||
         elapsed_ticks_exceeds(
             tick_index,
             upload.last_packet_tick,
             seconds_to_ticks(skins.uploads.idle_timeout_seconds, tick_rate));
}

void clear_active_skin_upload(
    SkinTransferSessionState& state,
    u32& active_skin_uploads_global) noexcept {
  if (!state.active_upload.has_value()) {
    return;
  }
  state.active_upload.reset();
  if (active_skin_uploads_global > 0U) {
    --active_skin_uploads_global;
  }
}

[[nodiscard]] std::optional<u64> regular_file_size(const std::filesystem::path& path) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return std::nullopt;
  }
  error.clear();
  const u64 size = std::filesystem::file_size(path, error);
  if (error) {
    return std::nullopt;
  }
  return size;
}

[[nodiscard]] std::string_view skin_request_deny_reason_name(
    protocol::SkinRequestDenyReason reason) noexcept {
  switch (reason) {
    case protocol::SkinRequestDenyReason::SkinsDisabled:
      return "SkinsDisabled";
    case protocol::SkinRequestDenyReason::InvalidHash:
      return "InvalidHash";
    case protocol::SkinRequestDenyReason::NotFound:
      return "NotFound";
    case protocol::SkinRequestDenyReason::NotAdvertisedToClient:
      return "NotAdvertisedToClient";
    case protocol::SkinRequestDenyReason::RateLimited:
      return "RateLimited";
    case protocol::SkinRequestDenyReason::TooLarge:
      return "TooLarge";
    case protocol::SkinRequestDenyReason::InternalError:
      return "InternalError";
  }
  return "Unknown";
}

[[nodiscard]] std::string_view skin_upload_deny_reason_name(
    protocol::SkinUploadDenyReason reason) noexcept {
  switch (reason) {
    case protocol::SkinUploadDenyReason::SkinsDisabled:
      return "SkinsDisabled";
    case protocol::SkinUploadDenyReason::UploadsDisabled:
      return "UploadsDisabled";
    case protocol::SkinUploadDenyReason::NotAuthenticated:
      return "NotAuthenticated";
    case protocol::SkinUploadDenyReason::InvalidFormat:
      return "InvalidFormat";
    case protocol::SkinUploadDenyReason::InvalidResolution:
      return "InvalidResolution";
    case protocol::SkinUploadDenyReason::ResolutionNotAllowed:
      return "ResolutionNotAllowed";
    case protocol::SkinUploadDenyReason::UploadTooLarge:
      return "UploadTooLarge";
    case protocol::SkinUploadDenyReason::RateLimited:
      return "RateLimited";
    case protocol::SkinUploadDenyReason::UploadAlreadyPending:
      return "UploadAlreadyPending";
    case protocol::SkinUploadDenyReason::ServerStorageFull:
      return "ServerStorageFull";
    case protocol::SkinUploadDenyReason::DecodeFailed:
      return "DecodeFailed";
    case protocol::SkinUploadDenyReason::HashMismatch:
      return "HashMismatch";
    case protocol::SkinUploadDenyReason::InternalError:
      return "InternalError";
  }
  return "Unknown";
}

[[nodiscard]] std::optional<std::vector<u8>> read_binary_file(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  return std::vector<u8>{
      std::istreambuf_iterator<char>{input},
      std::istreambuf_iterator<char>{}};
}

[[nodiscard]] player::PlayerCommand action_command_from_move(
    const protocol::ServerboundPlayerMove& move) noexcept {
  player::PlayerCommand command;
  command.client_tick = move.client_tick;
  command.primary_pressed =
      protocol::has_action_flag(move.action_flags, protocol::PlayerActionFlags::PrimaryPressed);
  command.primary_held =
      protocol::has_action_flag(move.action_flags, protocol::PlayerActionFlags::PrimaryHeld);
  command.secondary_pressed =
      protocol::has_action_flag(move.action_flags, protocol::PlayerActionFlags::SecondaryPressed);
  command.secondary_held =
      protocol::has_action_flag(move.action_flags, protocol::PlayerActionFlags::SecondaryHeld);
  command.toggle_fly_pressed =
      protocol::has_action_flag(move.action_flags, protocol::PlayerActionFlags::ToggleFlyPressed);
  command.selected_hotbar_slot = move.selected_hotbar_slot;
  command.sneak_held =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::Sneaking);
  command.sprint_held =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::Sprinting);
  return command;
}

[[nodiscard]] protocol::PlayerMoveFlags move_flags_from_player_state(
    const player::PlayerState& player_state) noexcept {
  protocol::PlayerMoveFlags flags = protocol::PlayerMoveFlags::None;
  if (player_state.grounded) {
    flags |= protocol::PlayerMoveFlags::OnGround;
  }
  if (player_state.sneaking) {
    flags |= protocol::PlayerMoveFlags::Sneaking;
  }
  if (player_state.swimming) {
    flags |= protocol::PlayerMoveFlags::Swimming;
  }
  if (player_state.movement_mode == player::MovementMode::Flying) {
    flags |= protocol::PlayerMoveFlags::Flying;
  }
  return flags;
}

[[nodiscard]] physics::PlayerCollisionPose collision_pose_from_flags(
    protocol::PlayerMoveFlags flags) noexcept {
  if (protocol::has_move_flag(flags, protocol::PlayerMoveFlags::Swimming)) {
    return physics::PlayerCollisionPose::Swimming;
  }
  if (protocol::has_move_flag(flags, protocol::PlayerMoveFlags::Sneaking)) {
    return physics::PlayerCollisionPose::Sneaking;
  }
  return physics::PlayerCollisionPose::Standing;
}

[[nodiscard]] MovementValidationContext make_movement_validation_context(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& block_registry,
    const player::PlayerState& player_state,
    const protocol::ServerboundPlayerMove& move,
    TickIndex tick_index,
    TickRate tick_rate,
    const player::PlayerTuning& tuning) {
  MovementValidationContext context;
  context.receive_time_seconds =
      static_cast<double>(tick_index) / static_cast<double>(std::max<TickRate>(tick_rate, 1U));
  context.server_tick = tick_index;
  context.can_fly = player_state.resolved_permissions.core.can_fly;
  context.has_collision = player_state.resolved_permissions.core.has_collision;
  context.server_velocity = player_state.velocity;
  context.knockback_control_suppression_ticks =
      player_state.knockback_control_suppression_ticks;

  const BlockPos destination_block{{
      static_cast<i32>(std::floor(move.position.x)),
      static_cast<i32>(std::floor(move.position.y)),
      static_cast<i32>(std::floor(move.position.z)),
  }};
  context.target_chunk_loaded = world.find_chunk(block_to_chunk(destination_block)) != nullptr;

  if (context.has_collision) {
    world::WorldCollisionQuery query(world, block_registry, false);
    const physics::KinematicConfig physics_config =
        physics::kinematic_config_from_tuning(tuning);
    const Aabb3f bounds = physics::make_body_bounds(
        move.position,
        physics_config,
        collision_pose_from_flags(move.flags));
    context.final_aabb_overlaps_solid = physics::overlaps_solid_blocks(
        query,
        bounds,
        physics_config.collision_epsilon);
  }
  return context;
}

[[nodiscard]] u32 knockback_suppression_ticks_to_consume(
    const AcceptedPlayerMovementState& previous,
    TickIndex tick_index,
    u32 remaining_ticks) noexcept {
  if (remaining_ticks == 0U) {
    return 0U;
  }
  if (!previous.has_accepted_move || tick_index <= previous.last_server_tick) {
    return 1U;
  }
  const TickIndex elapsed_ticks = tick_index - previous.last_server_tick;
  const TickIndex capped_ticks = std::min<TickIndex>(
      elapsed_ticks,
      static_cast<TickIndex>(remaining_ticks));
  return static_cast<u32>(std::max<TickIndex>(1U, capped_ticks));
}

} // namespace

net::NetworkResult send_protocol_message(
    net::ITransport& transport,
    net::ConnectionID connection_id,
    protocol::MessageType message_type,
    protocol::ReliabilityClass reliability,
    protocol::NetworkLane lane,
    std::vector<u8> payload) {
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, message_type, 0, 0, 0, 0},
      payload);
  return transport.send(
      {connection_id,
       reliability,
       std::move(packet),
       message_type,
       lane});
}

void log_send_failure_if_dedicated(
    ServerMode mode,
    net::ConnectionID connection_id,
    const net::NetworkResult& send_result) {
  if (send_result || mode != ServerMode::Dedicated) {
    return;
  }

  core::warn(
      "dedicated",
      "network send failed: connection={} reason='{}'",
      connection_id.value,
      send_result.error().message);
}

void ServerRuntimeImpl::expire_timed_out_skin_uploads() {
  for (auto& [session_id, transfer] : skin_transfers_) {
    (void)session_id;
    if (transfer.active_upload.has_value() &&
        skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
      clear_active_skin_upload(transfer, active_skin_uploads_global_);
    }
  }
}

void ServerRuntimeImpl::handle_network_event(net::NetworkEvent event) {
  switch (event.type) {
    case net::NetworkEventType::ConnectionRequested:
    case net::NetworkEventType::ConnectionAccepted:
      return;
    case net::NetworkEventType::ConnectionClosed:
    case net::NetworkEventType::ConnectionFailed:
      handle_connection_lost(event.connection_id);
      return;
    case net::NetworkEventType::MessageReceived:
      handle_message(event.connection_id, event.payload);
      return;
    case net::NetworkEventType::MessageSendFailed:
      if (mode_ == ServerMode::Dedicated) {
        core::warn(
            "dedicated",
            "network send failed: connection={} reason='{}'",
            event.connection_id.value,
            event.reason);
      }
      return;
  }
}

void ServerRuntimeImpl::handle_message(
    net::ConnectionID connection_id,
    const std::vector<u8>& packet) {
  auto decoded = protocol::deserialize_envelope(packet);
  if (!decoded) {
    kick_client(connection_id, "Invalid protocol packet.");
    return;
  }

  switch (decoded.value().envelope.message_type) {
    case protocol::MessageType::ServerboundHello: {
      auto client_hello = protocol::deserialize_serverbound_hello(decoded.value().payload);
      if (!client_hello) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_client_hello(connection_id, client_hello.value());
      return;
    }
    case protocol::MessageType::ServerboundAuthResponse: {
      auto response = protocol::deserialize_auth_response(decoded.value().payload);
      if (!response) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_auth_response(connection_id, response.value());
      return;
    }
    case protocol::MessageType::ServerboundKeepAlive: {
      auto keep_alive = protocol::deserialize_serverbound_keep_alive(decoded.value().payload);
      if (!keep_alive) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_keep_alive(connection_id, keep_alive.value());
      return;
    }
    case protocol::MessageType::ServerboundPlayerMove: {
      auto move = protocol::deserialize_serverbound_player_move(decoded.value().payload);
      if (!move) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_player_move(connection_id, move.value());
      return;
    }
    case protocol::MessageType::ServerboundChunkInterestUpdate: {
      auto interest = protocol::deserialize_serverbound_chunk_interest_update(decoded.value().payload);
      if (!interest) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_chunk_interest_update(connection_id, interest.value());
      return;
    }
    case protocol::MessageType::ServerboundInventoryActionRequest: {
      auto request = protocol::deserialize_serverbound_inventory_action_request(decoded.value().payload);
      if (!request) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_inventory_action_request(connection_id, request.value());
      return;
    }
    case protocol::MessageType::ServerboundChatMessage: {
      auto chat = protocol::deserialize_serverbound_chat_message(decoded.value().payload);
      if (!chat) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_chat_message(connection_id, std::move(chat.value()));
      return;
    }
    case protocol::MessageType::ServerboundAttackEntity: {
      auto attack = protocol::deserialize_serverbound_attack_entity(decoded.value().payload);
      if (!attack) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_attack_entity(connection_id, attack.value());
      return;
    }
    case protocol::MessageType::ServerboundSkinRequest: {
      auto request = protocol::deserialize_skin_request(decoded.value().payload);
      if (!request) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_skin_request(connection_id, request.value());
      return;
    }
    case protocol::MessageType::ServerboundSkinRequestBatch: {
      auto batch = protocol::deserialize_skin_request_batch(decoded.value().payload);
      if (!batch) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_skin_request_batch(connection_id, std::move(batch.value()));
      return;
    }
    case protocol::MessageType::ServerboundSkinUploadBegin: {
      auto begin = protocol::deserialize_skin_upload_begin(decoded.value().payload);
      if (!begin) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_skin_upload_begin(connection_id, begin.value());
      return;
    }
    case protocol::MessageType::ServerboundSkinUploadData: {
      auto data = protocol::deserialize_skin_upload_data(decoded.value().payload);
      if (!data) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_skin_upload_data(connection_id, std::move(data.value()));
      return;
    }
    case protocol::MessageType::ServerboundSkinUploadChunk: {
      auto chunk = protocol::deserialize_skin_upload_chunk(decoded.value().payload);
      if (!chunk) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_skin_upload_chunk(connection_id, std::move(chunk.value()));
      return;
    }
    case protocol::MessageType::ServerboundSkinUploadEnd: {
      auto end = protocol::deserialize_skin_upload_end(decoded.value().payload);
      if (!end) {
        kick_client(connection_id, "Invalid protocol packet.");
        return;
      }
      handle_skin_upload_end(connection_id, end.value());
      return;
    }
    default:
      return;
  }
}

void ServerRuntimeImpl::handle_client_hello(
    net::ConnectionID connection_id,
    const protocol::ServerboundHello& client_hello) {
  protocol::HandshakePolicy policy = handshake_policy_;
  if (!client_hello.auth_begin.has_value()) {
    kick_client(connection_id, "authentication required");
    return;
  }

  const protocol::ServerboundAuthBegin& auth_begin = client_hello.auth_begin.value();
  PlayerProfileOpenResult profile_result = profiles_.open_or_create(
      auth_begin.public_key,
      auth_begin.display_name,
      auth_begin.claimed_profile_id);
  if (profile_result.status == PlayerProfileOpenStatus::InvalidClaimedProfileID ||
      profile_result.status == PlayerProfileOpenStatus::PublicKeyMismatch ||
      profile_result.profile == nullptr) {
    kick_client(connection_id, "authentication failed");
    return;
  }

  SessionProfileIdentity profile_identity;
  profile_identity.profile_id = profile_result.profile->profile_id;
  profile_identity.display_name = profile_result.profile->display_name;
  profile_identity.public_key = profile_result.profile->public_key;
  profile_identity.has_public_key = true;

  if (ServerSession* existing = sessions_.find_by_connection(connection_id); existing != nullptr) {
    policy.assigned_session_id = existing->session_id;
    policy.assigned_player_id = existing->player_id;
    policy.initial_world_id = existing->world_id;
  } else {
    const ReservedSessionIdentity identity = sessions_.reserve_identity();
    policy.assigned_session_id = identity.session_id;
    policy.assigned_player_id = identity.player_id;
    policy.initial_world_id = DefaultWorldID;
  }

  protocol::ClientboundHello server_hello = protocol::make_clientbound_hello(client_hello, policy);
  if (server_hello.accepted && sessions_.find_by_connection(connection_id) == nullptr) {
    ServerSession& session = sessions_.create(
        connection_id,
        {server_hello.assigned_session_id, server_hello.assigned_player_id},
        server_hello.initial_world_id,
        std::move(profile_identity),
        mode_ == ServerMode::Integrated);
    if (mode_ == ServerMode::Integrated) {
      if (!complete_authenticated_session(session)) {
        return;
      }
    }
  }

  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundHello,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_clientbound_hello(server_hello));
  if (server_hello.accepted) {
    if (ServerSession* session = sessions_.find_by_connection(connection_id);
        session != nullptr) {
      if (mode_ == ServerMode::Integrated) {
        send_auth_result(*session, true);
        if (const player::PlayerState* player_state =
                default_world_.find_player(session->player_id);
            player_state != nullptr) {
          send_skin_rules(*session, *player_state);
          send_world_join_result(*session, *player_state);
          session->connection_state = net::ConnectionState::InGame;
          sessions_.for_each_session([&](const ServerSession& subject) {
            if (subject.authenticated && subject.world_id == session->world_id) {
              send_player_profile_info_to(*session, subject);
            }
          });
          broadcast_player_profile_info(*session);
        }
      } else if (!session->authenticated) {
        send_auth_challenge(*session);
      }
    }
  }
}

void ServerRuntimeImpl::handle_auth_response(
    net::ConnectionID connection_id,
    const protocol::ServerboundAuthResponse& response) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || session->authenticated || !session->challenge.has_value() ||
      !session->has_public_key) {
    return;
  }

  const PendingAuthChallenge challenge = session->challenge.value();
  session->challenge.reset();
  if (tick_index_ > challenge.expires_server_tick) {
    kick_client(connection_id, "authentication failed");
    return;
  }

  const std::vector<u8> payload_bytes = protocol::build_auth_signed_payload(
      {protocol::CurrentProtocolVersion,
       challenge.server_instance_id,
       session->session_id,
       session->profile_id,
       session->public_key,
       challenge.nonce,
       challenge.expires_server_tick});
  const auto* first = reinterpret_cast<const std::byte*>(payload_bytes.data());
  const bool verified = crypto::verify_ed25519(
      std::span<const std::byte>{first, payload_bytes.size()},
      response.signature,
      session->public_key);
  if (!verified) {
    kick_client(connection_id, "authentication failed");
    return;
  }

  if (!complete_authenticated_session(*session)) {
    return;
  }
  send_auth_result(*session, true);
  if (const player::PlayerState* player_state = default_world_.find_player(session->player_id);
      player_state != nullptr) {
    send_skin_rules(*session, *player_state);
    send_world_join_result(*session, *player_state);
    session->connection_state = net::ConnectionState::InGame;
    sessions_.for_each_session([&](const ServerSession& subject) {
      if (subject.authenticated && subject.world_id == session->world_id) {
        send_player_profile_info_to(*session, subject);
      }
    });
    broadcast_player_profile_info(*session);
  }
}

void ServerRuntimeImpl::handle_keep_alive(
    net::ConnectionID connection_id,
    const protocol::ServerboundKeepAlive& keep_alive) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated || session->closing) {
    return;
  }
  if (keep_alive.id != session->last_keep_alive_id) {
    return;
  }
  session->last_keep_alive_received_time = std::chrono::steady_clock::now();
}

void ServerRuntimeImpl::handle_player_move(
    net::ConnectionID connection_id,
    const protocol::ServerboundPlayerMove& move) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }

  player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr || block_registry_ == nullptr) {
    return;
  }

  refresh_player_permissions(*player_state);
  AcceptedPlayerMovementState& previous = accepted_player_movement_[session->player_id];
  const double receive_time_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(base_tps_, 1U));
  auto sync_previous_to_player_state = [&]() {
    previous.position = player_state->position;
    previous.velocity = player_state->velocity;
    previous.yaw_radians = player_state->yaw_radians;
    previous.pitch_radians = player_state->pitch_radians;
    previous.flags = move_flags_from_player_state(*player_state);
    previous.last_server_tick = tick_index_;
    previous.last_receive_time_seconds = receive_time_seconds;
    previous.has_accepted_move = true;
  };
  if (!previous.has_accepted_move) {
    sync_previous_to_player_state();
  } else if (length(player_state->position - previous.position) >
             movement_.base_position_epsilon_blocks) {
    sync_previous_to_player_state();
  }

  const player::PlayerCommand command = action_command_from_move(move);
  player::PlayerCommand& pending = pending_player_commands_[session->player_id];
  if (command.client_tick >= player_state->last_processed_input_tick &&
      command.client_tick >= pending.client_tick) {
    pending = command;
  }

  const MovementValidationContext context = make_movement_validation_context(
      default_world_,
      *block_registry_,
      *player_state,
      move,
      tick_index_,
      base_tps_,
      player_tuning_);
  const MovementValidationResult validation =
      validate_serverbound_player_move(move, previous, context, movement_);
  if (!validation.accepted) {
    if (validation.should_send_correction) {
      send_player_move_correction(*session, *player_state, move.sequence, validation.reason);
    }
    return;
  }

  const u32 consumed_knockback_suppression_ticks =
      knockback_suppression_ticks_to_consume(
          previous,
          tick_index_,
          player_state->knockback_control_suppression_ticks);
  previous = validation.accepted_state;
  player_state->position = validation.accepted_state.position;
  player_state->velocity = validation.accepted_state.velocity;
  player_state->yaw_radians = validation.accepted_state.yaw_radians;
  player_state->pitch_radians = validation.accepted_state.pitch_radians;
  player_state->grounded =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::OnGround);
  player_state->sneaking =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::Sneaking);
  player_state->swimming =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::Swimming);
  player_state->movement_mode =
      protocol::has_move_flag(move.flags, protocol::PlayerMoveFlags::Flying)
          ? player::MovementMode::Flying
          : player::MovementMode::Walking;
  const u32 remaining_knockback_suppression_ticks =
      player_state->knockback_control_suppression_ticks;
  player_state->knockback_control_suppression_ticks =
      remaining_knockback_suppression_ticks > consumed_knockback_suppression_ticks
          ? remaining_knockback_suppression_ticks -
                consumed_knockback_suppression_ticks
          : 0U;
}

void ServerRuntimeImpl::handle_chat_message(
    net::ConnectionID connection_id,
    const protocol::ServerboundChatMessage& message) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated || session->closing) {
    return;
  }

  const std::string channel = message.channel.value_or("arvane:global");
  if (std::string_view{channel} != protocol::ChatChannelGlobal) {
    send_error_message(
        *session,
        chat_formatter_.format_error_message("Unknown or unavailable chat channel."));
    return;
  }

  auto normalized = validate_chat_message_text(message.message, chat_validation_);
  if (!normalized) {
    send_error_message(
        *session,
        chat_formatter_.format_error_message(normalized.error().message));
    return;
  }

  const Seconds now_seconds =
      static_cast<Seconds>(tick_index_) / static_cast<Seconds>(std::max<TickRate>(base_tps_, 1U));
  ChatRateLimitState& rate_limit = chat_rate_limits_[session->session_id];
  if (!consume_chat_rate_limit(rate_limit, chat_validation_, now_seconds)) {
    send_error_message(
        *session,
        chat_formatter_.format_error_message("You are sending messages too quickly."));
    return;
  }

  ChatFormatContext context;
  context.sender_profile_id = session->profile_id;
  context.sender_name =
      session->display_name.empty() ? session->profile_name : session->display_name;
  context.channel = channel;
  context.raw_message = std::move(normalized.value());
  protocol::RichText formatted = chat_formatter_.format_player_chat(context);
  const protocol::RichTextValidation validation = protocol::validate_rich_text(formatted);
  if (!validation.valid) {
    core::warn("chat", "refusing to broadcast invalid formatted chat: {}", validation.message);
    send_error_message(
        *session,
        chat_formatter_.format_error_message("Chat message could not be formatted."));
    return;
  }

  const protocol::ClientboundChatMessage packet{
      next_chat_message_id_++,
      "arvane:global",
      protocol::ChatMessageKind::Chat,
      std::move(formatted)};
  const std::vector<u8> payload = protocol::serialize_clientbound_chat_message(packet);
  sessions_.for_each_session([&](const ServerSession& recipient) {
    if (!recipient.authenticated || recipient.closing) {
      return;
    }
    const net::NetworkResult send_result = send_protocol_message(
        *transport_,
        recipient.connection_id,
        protocol::MessageType::ClientboundChatMessage,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        payload);
    log_send_failure_if_dedicated(mode_, recipient.connection_id, send_result);
  });
  core::info(
      "chat",
      "[{}] {}: {}",
      packet.channel,
      context.sender_name,
      context.raw_message);
}

void ServerRuntimeImpl::send_system_message(
    const ServerSession& recipient,
    protocol::RichText message) {
  const protocol::RichTextValidation validation = protocol::validate_rich_text(message);
  if (!validation.valid) {
    core::warn("chat", "refusing to send invalid system chat: {}", validation.message);
    return;
  }
  const protocol::ClientboundChatMessage packet{
      next_chat_message_id_++,
      "arvane:system",
      protocol::ChatMessageKind::System,
      std::move(message)};
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      recipient.connection_id,
      protocol::MessageType::ClientboundChatMessage,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_chat_message(packet));
  log_send_failure_if_dedicated(mode_, recipient.connection_id, send_result);
}

void ServerRuntimeImpl::send_error_message(
    const ServerSession& recipient,
    protocol::RichText message) {
  const protocol::RichTextValidation validation = protocol::validate_rich_text(message);
  if (!validation.valid) {
    core::warn("chat", "refusing to send invalid error chat: {}", validation.message);
    return;
  }
  const protocol::ClientboundChatMessage packet{
      next_chat_message_id_++,
      "arvane:system",
      protocol::ChatMessageKind::Error,
      std::move(message)};
  const net::NetworkResult send_result = send_protocol_message(
      *transport_,
      recipient.connection_id,
      protocol::MessageType::ClientboundChatMessage,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_chat_message(packet));
  log_send_failure_if_dedicated(mode_, recipient.connection_id, send_result);
}

void ServerRuntimeImpl::broadcast_system_message(protocol::RichText message) {
  const protocol::RichTextValidation validation = protocol::validate_rich_text(message);
  if (!validation.valid) {
    core::warn("chat", "refusing to broadcast invalid system chat: {}", validation.message);
    return;
  }
  const protocol::ClientboundChatMessage packet{
      next_chat_message_id_++,
      "arvane:system",
      protocol::ChatMessageKind::System,
      std::move(message)};
  const std::vector<u8> payload = protocol::serialize_clientbound_chat_message(packet);
  sessions_.for_each_session([&](const ServerSession& recipient) {
    if (!recipient.authenticated || recipient.closing) {
      return;
    }
    const net::NetworkResult send_result = send_protocol_message(
        *transport_,
        recipient.connection_id,
        protocol::MessageType::ClientboundChatMessage,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Gameplay,
        payload);
    log_send_failure_if_dedicated(mode_, recipient.connection_id, send_result);
  });
}

void ServerRuntimeImpl::handle_skin_request(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinRequest& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  auto send_deny = [&](protocol::SkinRequestDenyReason reason) {
    core::debug(
        "skin",
        "skin request denied: connection={} request={} skin={} reason={}",
        connection_id.value,
        request.request_id,
        request.skin_hash,
        skin_request_deny_reason_name(reason));
    const protocol::ClientboundSkinRequestDeny deny{
        request.request_id,
        request.skin_hash,
        reason};
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinRequestDeny,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_request_deny(deny));
  };

  if (session == nullptr || !session->authenticated) {
    return;
  }
  if (!skins_.enabled) {
    send_deny(protocol::SkinRequestDenyReason::SkinsDisabled);
    return;
  }
  if (!protocol::is_valid_skin_hash(request.skin_hash)) {
    send_deny(protocol::SkinRequestDenyReason::InvalidHash);
    return;
  }

  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  if (!transfer.request_second_limiter.try_consume(1.0, now_seconds) ||
      !transfer.request_minute_limiter.try_consume(1.0, now_seconds)) {
    send_deny(protocol::SkinRequestDenyReason::RateLimited);
    return;
  }
  if (transfer.in_flight_request_ids.find(request.request_id) !=
          transfer.in_flight_request_ids.end() ||
      transfer.in_flight_skin_hashes.find(request.skin_hash) !=
          transfer.in_flight_skin_hashes.end() ||
      transfer.in_flight_request_ids.size() >=
          skins_.downloads.max_in_flight_per_connection) {
    send_deny(protocol::SkinRequestDenyReason::RateLimited);
    return;
  }
  if (skins_.downloads.require_advertised_hash &&
      transfer.advertised_skin_hashes.find(request.skin_hash) ==
          transfer.advertised_skin_hashes.end()) {
    send_deny(protocol::SkinRequestDenyReason::NotAdvertisedToClient);
    return;
  }
  auto descriptor_result = load_stored_skin_descriptor(server_save_root_, request.skin_hash);
  if (!descriptor_result) {
    send_deny(descriptor_result.error());
    return;
  }
  const protocol::SkinDescriptor descriptor = descriptor_result.value();
  const io::SkinResolutionConfig* resolution =
      descriptor.width == descriptor.height
          ? find_skin_resolution_rule(skins_, descriptor.width)
          : nullptr;
  if (resolution == nullptr || descriptor.width == 0U || descriptor.height == 0U) {
    send_deny(protocol::SkinRequestDenyReason::InternalError);
    return;
  }
  const std::filesystem::path path =
      server_skin_storage_path(server_save_root_, request.skin_hash);
  const std::optional<u64> file_size = regular_file_size(path);
  if (!file_size.has_value()) {
    send_deny(protocol::SkinRequestDenyReason::NotFound);
    return;
  }
  if (file_size.value() == 0U ||
      file_size.value() > resolution->max_upload_bytes ||
      file_size.value() > protocol::MaxSkinImagePayloadBytes) {
    send_deny(protocol::SkinRequestDenyReason::TooLarge);
    return;
  }
  if (!transfer.download_byte_limiter.try_consume(
          static_cast<double>(file_size.value()),
          now_seconds)) {
    send_deny(protocol::SkinRequestDenyReason::RateLimited);
    return;
  }

  std::optional<std::vector<u8>> bytes = read_binary_file(path);
  if (!bytes.has_value()) {
    send_deny(protocol::SkinRequestDenyReason::NotFound);
    return;
  }
  if (bytes->size() != file_size.value()) {
    send_deny(protocol::SkinRequestDenyReason::InternalError);
    return;
  }

  transfer.in_flight_request_ids.insert(request.request_id);
  transfer.in_flight_skin_hashes.insert(request.skin_hash);
  auto finish_request = [&]() {
    transfer.in_flight_request_ids.erase(request.request_id);
    transfer.in_flight_skin_hashes.erase(request.skin_hash);
  };
  const bool chunked =
      bytes->size() >= skins_.uploads.chunk_threshold_bytes ||
      descriptor.width >= 512U ||
      descriptor.height >= 512U;
  if (!chunked) {
    protocol::ClientboundSkinData data;
    data.request_id = request.request_id;
    data.skin_hash = request.skin_hash;
    data.format = descriptor.format;
    data.width = descriptor.width;
    data.height = descriptor.height;
    data.compressed_image_bytes = std::move(*bytes);
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinData,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::ChunkStreaming,
        protocol::serialize_skin_data(data));
    finish_request();
    return;
  }

  protocol::ClientboundSkinDataBeginChunked begin;
  begin.request_id = request.request_id;
  begin.skin_hash = request.skin_hash;
  begin.format = descriptor.format;
  begin.width = descriptor.width;
  begin.height = descriptor.height;
  begin.total_bytes = static_cast<u32>(bytes->size());
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundSkinDataBeginChunked,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::ChunkStreaming,
      protocol::serialize_skin_data_begin_chunked(begin));
  usize offset = 0;
  while (offset < bytes->size()) {
    const usize chunk_size = std::min<usize>(
        skins_.uploads.max_chunk_bytes,
        bytes->size() - offset);
    protocol::ClientboundSkinDataChunk chunk;
    chunk.request_id = request.request_id;
    chunk.chunk.insert(
        chunk.chunk.end(),
        bytes->begin() + static_cast<std::ptrdiff_t>(offset),
        bytes->begin() + static_cast<std::ptrdiff_t>(offset + chunk_size));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinDataChunk,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::ChunkStreaming,
        protocol::serialize_skin_data_chunk(chunk));
    offset += chunk_size;
  }
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundSkinDataEndChunked,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::ChunkStreaming,
      protocol::serialize_skin_data_end_chunked({request.request_id}));
  finish_request();
}

void ServerRuntimeImpl::handle_skin_request_batch(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinRequestBatch& batch) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }

  auto send_deny = [&](const protocol::SkinRequestEntry& request) {
    core::debug(
        "skin",
        "skin request denied: connection={} request={} skin={} reason={}",
        connection_id.value,
        request.request_id,
        request.skin_hash,
        skin_request_deny_reason_name(protocol::SkinRequestDenyReason::RateLimited));
    const protocol::ClientboundSkinRequestDeny deny{
        request.request_id,
        request.skin_hash,
        protocol::SkinRequestDenyReason::RateLimited};
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinRequestDeny,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_request_deny(deny));
  };

  if (batch.requests.size() > skins_.downloads.max_batch_size) {
    for (const protocol::SkinRequestEntry& request : batch.requests) {
      send_deny(request);
    }
    return;
  }

  for (const protocol::SkinRequestEntry& entry : batch.requests) {
    handle_skin_request(connection_id, {entry.request_id, entry.skin_hash});
  }
}

void ServerRuntimeImpl::handle_skin_upload_begin(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadBegin& begin) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  auto send_deny = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload denied: connection={} reason={} width={} height={} bytes={}",
        connection_id.value,
        skin_upload_deny_reason_name(reason),
        begin.width,
        begin.height,
        begin.total_upload_bytes);
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinUploadDeny,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_deny({reason}));
  };
  if (session == nullptr) {
    send_deny(protocol::SkinUploadDenyReason::NotAuthenticated);
    return;
  }
  if (!session->authenticated) {
    send_deny(protocol::SkinUploadDenyReason::NotAuthenticated);
    return;
  }
  player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr) {
    send_deny(protocol::SkinUploadDenyReason::NotAuthenticated);
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  if (transfer.active_upload.has_value() &&
      skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
  }
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  const bool rate_limited =
      !transfer.upload_begin_limiter.try_consume(1.0, now_seconds) ||
      transfer.failed_decode_limiter.available(now_seconds) < 1.0;
  const auto accepted = validate_skin_upload_begin(
      skins_,
      player_state->resolved_permissions,
      session->authenticated,
      transfer.active_upload.has_value(),
      rate_limited,
      active_skin_uploads_global_ >= skins_.uploads.max_active_global,
      begin);
  if (!accepted) {
    send_deny(accepted.error());
    return;
  }

  const u32 upload_id = transfer.next_upload_id++;
  if (!begin.client_skin_hash_hint.empty()) {
    auto existing_descriptor =
        load_stored_skin_descriptor(server_save_root_, begin.client_skin_hash_hint);
    if (existing_descriptor &&
        existing_descriptor.value().width == begin.width &&
        existing_descriptor.value().height == begin.height) {
      protocol::SkinDescriptor descriptor = std::move(existing_descriptor.value());
      descriptor.slim_model = begin.slim_model;
      set_selected_skin_for_session(*session, descriptor.skin_hash, descriptor.slim_model);
      core::info(
          "skin",
          "skin upload complete: profile={} skin={} bytes=0",
          session->profile_id.string(),
          descriptor.skin_hash);
      (void)send_protocol_message(
          *transport_,
          connection_id,
          protocol::MessageType::ClientboundSkinUploadComplete,
          protocol::ReliabilityClass::ReliableOrdered,
          protocol::NetworkLane::Session,
          protocol::serialize_skin_upload_complete(
              {upload_id, std::move(descriptor)}));
      broadcast_player_profile_info(*session);
      return;
    }
  }

  if (!transfer.upload_byte_limiter.try_consume(
          static_cast<double>(begin.total_upload_bytes),
          now_seconds)) {
    send_deny(protocol::SkinUploadDenyReason::RateLimited);
    return;
  }

  ActiveSkinUpload upload;
  upload.upload_id = upload_id;
  upload.begin = begin;
  upload.mode = accepted.value().upload_mode;
  upload.max_chunk_bytes = skins_.uploads.max_chunk_bytes;
  upload.started_tick = tick_index_;
  upload.last_packet_tick = tick_index_;
  upload.session_id = session->session_id;
  upload.bytes.reserve(begin.total_upload_bytes);
  transfer.active_upload = std::move(upload);
  ++active_skin_uploads_global_;

  protocol::ClientboundSkinUploadAccept packet;
  packet.upload_id = upload_id;
  packet.upload_mode = accepted.value().upload_mode;
  packet.max_chunk_bytes = skins_.uploads.max_chunk_bytes;
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundSkinUploadAccept,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_upload_accept(packet));
}

void ServerRuntimeImpl::handle_skin_upload_data(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadData& data) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  auto send_failed = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload failed: profile={} upload={} reason={}",
        session->profile_id.string(),
        data.upload_id,
        skin_upload_deny_reason_name(reason));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinUploadFailed,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_failed({data.upload_id, reason}));
  };
  if (!transfer.active_upload.has_value() ||
      transfer.active_upload->upload_id != data.upload_id) {
    return;
  }
  if (skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    return;
  }
  if (transfer.active_upload->mode != protocol::SkinUploadMode::Full) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::InternalError);
    return;
  }
  if (data.compressed_image_bytes.size() !=
          transfer.active_upload->begin.total_upload_bytes ||
      data.compressed_image_bytes.size() >
          transfer.active_upload->begin.total_upload_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  if (transfer.completed_upload_minute_limiter.available(now_seconds) < 1.0 ||
      transfer.completed_upload_hour_limiter.available(now_seconds) < 1.0) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::RateLimited);
    return;
  }

  ActiveSkinUpload upload = std::move(*transfer.active_upload);
  clear_active_skin_upload(transfer, active_skin_uploads_global_);
  const io::SkinResolutionConfig* resolution =
      find_skin_resolution_rule(skins_, upload.begin.width);
  if (resolution == nullptr) {
    send_failed(protocol::SkinUploadDenyReason::InvalidResolution);
    return;
  }
  u64 added_storage_bytes = 0U;
  auto descriptor = validate_and_store_uploaded_skin(
      server_save_root_,
      *resolution,
      upload.begin,
      data.compressed_image_bytes,
      skin_storage_bytes_,
      skins_.storage.max_total_skin_bytes,
      &added_storage_bytes);
  if (!descriptor) {
    if (descriptor.error() == protocol::SkinUploadDenyReason::DecodeFailed) {
      (void)transfer.failed_decode_limiter.try_consume(1.0, now_seconds);
    }
    send_failed(descriptor.error());
    return;
  }
  (void)transfer.completed_upload_minute_limiter.try_consume(1.0, now_seconds);
  (void)transfer.completed_upload_hour_limiter.try_consume(1.0, now_seconds);
  skin_storage_bytes_ += added_storage_bytes;
  set_selected_skin_for_session(
      *session,
      descriptor.value().skin_hash,
      descriptor.value().slim_model);
  core::info(
      "skin",
      "skin upload complete: profile={} skin={} bytes={}",
      session->profile_id.string(),
      descriptor.value().skin_hash,
      data.compressed_image_bytes.size());
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundSkinUploadComplete,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_upload_complete({data.upload_id, descriptor.value()}));
  broadcast_player_profile_info(*session);
}

void ServerRuntimeImpl::handle_skin_upload_chunk(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadChunk& chunk) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  auto send_failed = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload failed: profile={} upload={} reason={}",
        session->profile_id.string(),
        chunk.upload_id,
        skin_upload_deny_reason_name(reason));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinUploadFailed,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_failed({chunk.upload_id, reason}));
  };
  if (!transfer.active_upload.has_value() ||
      transfer.active_upload->upload_id != chunk.upload_id) {
    return;
  }
  if (skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    return;
  }
  if (transfer.active_upload->mode != protocol::SkinUploadMode::Chunked) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::InternalError);
    return;
  }
  if (chunk.image_chunk_bytes.empty() ||
      chunk.image_chunk_bytes.size() > transfer.active_upload->max_chunk_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  if (transfer.active_upload->bytes.size() + chunk.image_chunk_bytes.size() >
      transfer.active_upload->begin.total_upload_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  transfer.active_upload->bytes.insert(
      transfer.active_upload->bytes.end(),
      chunk.image_chunk_bytes.begin(),
      chunk.image_chunk_bytes.end());
  transfer.active_upload->last_packet_tick = tick_index_;
}

void ServerRuntimeImpl::handle_skin_upload_end(
    net::ConnectionID connection_id,
    const protocol::ServerboundSkinUploadEnd& end) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  SkinTransferSessionState& transfer = skin_transfers_[session->session_id];
  configure_skin_transfer_state(transfer, skins_);
  auto send_failed = [&](protocol::SkinUploadDenyReason reason) {
    core::debug(
        "skin",
        "skin upload failed: profile={} upload={} reason={}",
        session->profile_id.string(),
        end.upload_id,
        skin_upload_deny_reason_name(reason));
    (void)send_protocol_message(
        *transport_,
        connection_id,
        protocol::MessageType::ClientboundSkinUploadFailed,
        protocol::ReliabilityClass::ReliableOrdered,
        protocol::NetworkLane::Session,
        protocol::serialize_skin_upload_failed({end.upload_id, reason}));
  };
  if (!transfer.active_upload.has_value() ||
      transfer.active_upload->upload_id != end.upload_id) {
    return;
  }
  if (skin_upload_timed_out(*transfer.active_upload, tick_index_, skins_, base_tps_)) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    return;
  }
  if (transfer.active_upload->mode != protocol::SkinUploadMode::Chunked) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::InternalError);
    return;
  }
  if (transfer.active_upload->bytes.size() !=
      transfer.active_upload->begin.total_upload_bytes) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::UploadTooLarge);
    return;
  }
  const double now_seconds =
      static_cast<double>(tick_index_) / static_cast<double>(std::max<TickRate>(1U, base_tps_));
  if (transfer.completed_upload_minute_limiter.available(now_seconds) < 1.0 ||
      transfer.completed_upload_hour_limiter.available(now_seconds) < 1.0) {
    clear_active_skin_upload(transfer, active_skin_uploads_global_);
    send_failed(protocol::SkinUploadDenyReason::RateLimited);
    return;
  }
  ActiveSkinUpload upload = std::move(*transfer.active_upload);
  clear_active_skin_upload(transfer, active_skin_uploads_global_);
  const io::SkinResolutionConfig* resolution =
      find_skin_resolution_rule(skins_, upload.begin.width);
  if (resolution == nullptr) {
    send_failed(protocol::SkinUploadDenyReason::InvalidResolution);
    return;
  }
  u64 added_storage_bytes = 0U;
  auto descriptor = validate_and_store_uploaded_skin(
      server_save_root_,
      *resolution,
      upload.begin,
      upload.bytes,
      skin_storage_bytes_,
      skins_.storage.max_total_skin_bytes,
      &added_storage_bytes);
  if (!descriptor) {
    if (descriptor.error() == protocol::SkinUploadDenyReason::DecodeFailed) {
      (void)transfer.failed_decode_limiter.try_consume(1.0, now_seconds);
    }
    send_failed(descriptor.error());
    return;
  }
  (void)transfer.completed_upload_minute_limiter.try_consume(1.0, now_seconds);
  (void)transfer.completed_upload_hour_limiter.try_consume(1.0, now_seconds);
  skin_storage_bytes_ += added_storage_bytes;
  set_selected_skin_for_session(
      *session,
      descriptor.value().skin_hash,
      descriptor.value().slim_model);
  core::info(
      "skin",
      "skin upload complete: profile={} skin={} bytes={}",
      session->profile_id.string(),
      descriptor.value().skin_hash,
      upload.bytes.size());
  (void)send_protocol_message(
      *transport_,
      connection_id,
      protocol::MessageType::ClientboundSkinUploadComplete,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Session,
      protocol::serialize_skin_upload_complete({end.upload_id, descriptor.value()}));
  broadcast_player_profile_info(*session);
}

void ServerRuntimeImpl::handle_chunk_interest_update(
    net::ConnectionID connection_id,
    const protocol::ServerboundChunkInterestUpdate& interest) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated) {
    return;
  }
  const player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr) {
    return;
  }

  const ChunkSectionCoord authoritative_center = player_chunk_coord(*player_state);
  chunk_streamer_.update_player_interest(
      *session,
      authoritative_center,
      default_world_.height_settings(),
      interest);
  if (mode_ == ServerMode::Dedicated &&
      logged_chunk_interest_sessions_.insert(session->session_id).second) {
    core::info(
        "dedicated",
        "client '{}' requested chunk stream: session={} player={} center=({}, {}, {}) requested_view={} server_max_view={}",
        session->profile_name,
        session->session_id.value,
        session->player_id.value,
        authoritative_center.value.x,
        authoritative_center.value.y,
        authoritative_center.value.z,
        interest.client_view_distance,
        max_view_distance_);
  }
}

void ServerRuntimeImpl::handle_inventory_action_request(
    net::ConnectionID connection_id,
    const protocol::ServerboundInventoryActionRequest& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated ||
      request.inventory_id != protocol::PlayerInventoryID) {
    return;
  }

  switch (request.kind) {
    case protocol::InventoryActionKind::SlotClick:
      (void)apply_player_inventory_click(
          session->player_id,
          request.slot_index,
          request.button);
      return;
    case protocol::InventoryActionKind::DropCursor:
      (void)drop_player_inventory_cursor(session->player_id);
      return;
    case protocol::InventoryActionKind::DropOneCursor:
      (void)drop_one_from_player_inventory_cursor(session->player_id);
      return;
    case protocol::InventoryActionKind::CloseMenu:
      (void)close_player_inventory_menu(session->player_id);
      return;
  }
}

} // namespace arv::server::detail
