#include "arv/server/server_runtime.hpp"

#include "arv/physics/collision.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <optional>
#include <sstream>
#include <utility>

namespace arv::server {
namespace {

[[nodiscard]] arv::Vec3f player_look_direction(const player::PlayerState& player_state) noexcept {
  const f32 cos_pitch = std::cos(player_state.pitch_radians);
  return arv::normalize_or_zero(
      {std::sin(player_state.yaw_radians) * cos_pitch,
       std::sin(player_state.pitch_radians),
       std::cos(player_state.yaw_radians) * cos_pitch});
}

[[nodiscard]] ChunkCoord player_chunk_coord(const player::PlayerState& player_state) noexcept {
  return block_to_chunk({{
      static_cast<i32>(std::floor(player_state.position.x)),
      static_cast<i32>(std::floor(player_state.position.y)),
      static_cast<i32>(std::floor(player_state.position.z)),
  }});
}

[[nodiscard]] arv::Vec3f horizontal_forward(f32 yaw_radians) noexcept {
  return arv::normalize_or_zero({std::sin(yaw_radians), 0.0F, std::cos(yaw_radians)});
}

[[nodiscard]] arv::Vec3f horizontal_right(f32 yaw_radians) noexcept {
  return arv::normalize_or_zero({std::cos(yaw_radians), 0.0F, -std::sin(yaw_radians)});
}

[[nodiscard]] arv::Vec3f player_eye_position(
    const player::PlayerState& player_state,
    const player::PlayerTuning& tuning) noexcept {
  const f32 eye_height =
      player_state.sneaking ? tuning.sneaking_eye_height : tuning.standing_eye_height;
  return {
      player_state.position.x,
      player_state.position.y + eye_height,
      player_state.position.z};
}

void step_player_without_collision(
    player::PlayerState& player_state,
    const player::PlayerCommand& command,
    const player::PlayerTuning& tuning,
    Seconds tick_seconds) noexcept {
  Vec3f move{};
  if (command.move_z != 0) {
    move = move + horizontal_forward(player_state.yaw_radians) * static_cast<f32>(command.move_z);
  }
  if (command.move_x != 0) {
    move = move + horizontal_right(player_state.yaw_radians) * static_cast<f32>(command.move_x);
  }
  move = normalize_or_zero(move);

  player_state.velocity.x = move.x * tuning.fly_speed;
  player_state.velocity.z = move.z * tuning.fly_speed;

  f32 vertical = 0.0F;
  if (command.jump_held) {
    vertical += 1.0F;
  }
  if (command.sneak_held) {
    vertical -= 1.0F;
  }
  player_state.velocity.y = vertical * tuning.fly_vertical_speed;
  player_state.position = player_state.position + player_state.velocity * tick_seconds;
  player_state.grounded = false;
  player_state.sneaking = false;
}

[[nodiscard]] arv::BlockPos offset_block(arv::BlockPos pos, arv::voxel::BlockFace face) noexcept {
  switch (face) {
    case arv::voxel::BlockFace::PositiveX:
      ++pos.value.x;
      break;
    case arv::voxel::BlockFace::NegativeX:
      --pos.value.x;
      break;
    case arv::voxel::BlockFace::PositiveY:
      ++pos.value.y;
      break;
    case arv::voxel::BlockFace::NegativeY:
      --pos.value.y;
      break;
    case arv::voxel::BlockFace::PositiveZ:
      ++pos.value.z;
      break;
    case arv::voxel::BlockFace::NegativeZ:
      --pos.value.z;
      break;
  }
  return pos;
}

[[nodiscard]] protocol::PlayerCorrection make_player_correction(
    TickIndex server_tick,
    const player::PlayerState& player_state) {
  protocol::PlayerCorrection correction;
  correction.server_tick = server_tick;
  correction.player_id = player_state.player_id;
  correction.world_id = player_state.world_id;
  correction.position = player_state.position;
  correction.velocity = player_state.velocity;
  correction.yaw_radians = player_state.yaw_radians;
  correction.pitch_radians = player_state.pitch_radians;
  correction.movement_mode = player_state.movement_mode;
  correction.game_mode_id = player_state.game_mode_id;
  correction.resolved_core_permissions = player_state.resolved_permissions.core;
  correction.grounded = player_state.grounded;
  correction.sneaking = player_state.sneaking;
  correction.selected_hotbar_slot = player_state.selected_hotbar_slot;
  return correction;
}

[[nodiscard]] bool same_correction_payload(
    const protocol::PlayerCorrection& lhs,
    const protocol::PlayerCorrection& rhs) {
  return lhs.player_id == rhs.player_id &&
         lhs.world_id == rhs.world_id &&
         lhs.position == rhs.position &&
         lhs.velocity == rhs.velocity &&
         lhs.yaw_radians == rhs.yaw_radians &&
         lhs.pitch_radians == rhs.pitch_radians &&
         lhs.movement_mode == rhs.movement_mode &&
         lhs.game_mode_id == rhs.game_mode_id &&
         lhs.resolved_core_permissions == rhs.resolved_core_permissions &&
         lhs.grounded == rhs.grounded &&
         lhs.sneaking == rhs.sneaking &&
         lhs.selected_hotbar_slot == rhs.selected_hotbar_slot;
}

} // namespace

ServerRuntime::ServerRuntime(std::unique_ptr<net::ITransport> transport) : transport_(std::move(transport)) {
}

net::NetworkResult ServerRuntime::start(const ServerStartInfo& start_info) {
  if (!transport_) {
    return net::NetworkResult::failure(net::NetworkError{"server has no transport"});
  }

  mode_ = start_info.mode;
  base_tps_ = start_info.base_tps;
  block_registry_ = start_info.block_registry;
  item_registry_ = start_info.item_registry;
  player_tuning_ = start_info.player_tuning;
  fluid_definition_ = start_info.fluid_definition;
  chunk_streamer_.configure(start_info.chunk_streaming);
  chunk_generation_ = start_info.chunk_generation;
  max_pending_generation_jobs_ =
      std::max<u32>(start_info.chunk_streaming.max_chunks_sent_per_player_tick, 1U);
  worlds_.ensure_default_world();
  handshake_policy_.server_name =
      mode_ == ServerMode::Dedicated ? "Arvane Dedicated Server" : "Arvane Integrated Server";
  handshake_policy_.initial_world_id = DefaultWorldID;

  if (block_registry_ == nullptr || item_registry_ == nullptr) {
    return net::NetworkResult::failure(net::NetworkError{"server start requires block and item registries"});
  }

  auto listen_result = transport_->start_listening(start_info.listen_address);
  if (!listen_result) {
    return listen_result;
  }

  state_ = ServerRuntimeState::Running;
  return net::NetworkResult::success();
}

void ServerRuntime::tick() {
  if (state_ != ServerRuntimeState::Running || !transport_) {
    return;
  }

  drain_generation_jobs();
  drain_lighting_jobs();
  inbound_events_ = transport_->poll_events();
  for (net::NetworkEvent& event : inbound_events_) {
    handle_network_event(std::move(event));
  }
  tick_players();
  (void)default_world_.tick(*block_registry_, fluid_definition_);
  schedule_lighting_jobs();
  tick_chunk_streaming();
  schedule_generation_jobs();
  send_player_corrections();
  ++tick_index_;
}

void ServerRuntime::shutdown() {
  if (state_ == ServerRuntimeState::Stopped) {
    return;
  }

  state_ = ServerRuntimeState::Stopping;
  world_jobs_.wait_for_all();
  drain_generation_jobs();
  drain_lighting_jobs();
  if (transport_) {
    transport_->shutdown();
  }
  state_ = ServerRuntimeState::Stopped;
}

bool ServerRuntime::set_player_game_mode(PlayerID player_id, std::string_view mode_id) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || game_modes_.find(mode_id) == nullptr) {
    return false;
  }

  player_state->game_mode_id = std::string{mode_id};
  refresh_player_permissions(*player_state);
  return true;
}

void ServerRuntime::handle_network_event(net::NetworkEvent event) {
  switch (event.type) {
    case net::NetworkEventType::ConnectionRequested:
    case net::NetworkEventType::ConnectionAccepted:
      return;
    case net::NetworkEventType::ConnectionClosed:
    case net::NetworkEventType::ConnectionFailed:
      if (const ServerSession* session = sessions_.find_by_connection(event.connection_id);
          session != nullptr) {
        chunk_streamer_.remove_session(session->session_id);
        pending_player_commands_.erase(session->player_id);
        last_sent_player_corrections_.erase(session->player_id);
      }
      sessions_.remove_by_connection(event.connection_id);
      return;
    case net::NetworkEventType::MessageReceived:
      handle_message(event.connection_id, event.payload);
      return;
    case net::NetworkEventType::MessageSendFailed:
      return;
  }
}

void ServerRuntime::handle_message(net::ConnectionID connection_id, const std::vector<u8>& packet) {
  auto decoded = protocol::deserialize_envelope(packet);
  if (!decoded) {
    return;
  }

  switch (decoded.value().envelope.message_type) {
    case protocol::MessageType::ClientHello: {
      auto client_hello = protocol::deserialize_client_hello(decoded.value().payload);
      if (!client_hello) {
        return;
      }

      protocol::HandshakePolicy policy = handshake_policy_;
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

      protocol::ServerHello server_hello = protocol::make_server_hello(client_hello.value(), policy);
      if (server_hello.accepted && sessions_.find_by_connection(connection_id) == nullptr) {
        ServerSession& session = sessions_.create(
            connection_id,
            {server_hello.assigned_session_id, server_hello.assigned_player_id},
            server_hello.initial_world_id,
            client_hello.value().requested_username_or_profile_name);
        player::PlayerState& player_state = default_world_.ensure_player(session.player_id);
        player_state.world_id = session.world_id;
        seed_player_inventory(player_state);
        refresh_player_permissions(player_state);
      }

      std::vector<u8> payload = protocol::serialize_server_hello(server_hello);
      std::vector<u8> reply = protocol::serialize_envelope(
          {protocol::CurrentProtocolVersion, protocol::MessageType::ServerHello, 0, 0, 0, 0},
          payload);
      (void)transport_->send({connection_id, protocol::ReliabilityClass::ReliableOrdered, std::move(reply)});
      return;
    }
    case protocol::MessageType::ClientInputBatch: {
      auto batch = protocol::deserialize_client_input_batch(decoded.value().payload);
      if (!batch) {
        return;
      }
      handle_input_batch(connection_id, batch.value());
      return;
    }
    case protocol::MessageType::ChunkInterestUpdate: {
      auto interest = protocol::deserialize_chunk_interest_update(decoded.value().payload);
      if (!interest) {
        return;
      }
      handle_chunk_interest_update(connection_id, interest.value());
      return;
    }
    default:
      return;
  }
}

void ServerRuntime::handle_input_batch(
    net::ConnectionID connection_id,
    const protocol::ClientInputBatch& batch) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr) {
    return;
  }

  for (usize index = 0; index < batch.inputs.size(); ++index) {
    const TickIndex input_tick = batch.first_input_tick + index;
    player::PlayerCommand command =
        protocol::input_sample_to_command(batch.inputs[index], input_tick);
    player::PlayerCommand& pending = pending_player_commands_[session->player_id];
    if (command.client_tick >= pending.client_tick) {
      pending = std::move(command);
    }
  }
}

void ServerRuntime::tick_players() {
  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr) {
      continue;
    }

    player::PlayerState* player_state = default_world_.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    player::PlayerCommand command;
    if (auto found = pending_player_commands_.find(session->player_id);
        found != pending_player_commands_.end()) {
      command = found->second;
      pending_player_commands_.erase(found);
    } else {
      command.client_tick = player_state->last_processed_input_tick + 1U;
      command.selected_hotbar_slot = player_state->selected_hotbar_slot;
    }

    apply_player_command(*player_state, command);
    player_state->last_processed_input_tick = command.client_tick;
  }
}

void ServerRuntime::handle_chunk_interest_update(
    net::ConnectionID connection_id,
    const protocol::ChunkInterestUpdate& interest) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr) {
    return;
  }
  const player::PlayerState* player_state = default_world_.find_player(session->player_id);
  if (player_state == nullptr) {
    return;
  }

  chunk_streamer_.update_player_interest(*session, player_chunk_coord(*player_state), interest);
}

void ServerRuntime::tick_chunk_streaming() {
  queued_chunk_stream_messages_.clear();
  chunk_streamer_.tick_streaming_budget(
      default_world_,
      queued_chunk_stream_messages_);

  for (QueuedChunkStreamMessage& message : queued_chunk_stream_messages_) {
    std::vector<u8> packet = protocol::serialize_envelope(
        {protocol::CurrentProtocolVersion, message.message_type, 0, 0, 0, 0},
        message.payload);
    (void)transport_->send(
        {message.connection_id, message.reliability, std::move(packet)});
  }
}

void ServerRuntime::drain_generation_jobs() {
  for (usize index = 0; index < pending_generation_jobs_.size();) {
    PendingGenerationJob& pending = pending_generation_jobs_[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    world::GeneratedChunkResult generated = pending.job.get();
    voxel::ChunkSection* chunk = default_world_.find_chunk(pending.coord);
    if (generated &&
        chunk != nullptr &&
        chunk->lifecycle_state() == voxel::ChunkLifecycleState::Generating) {
      (void)default_world_.publish_generated_chunk(std::move(generated.value()));
    } else if (chunk != nullptr &&
               chunk->lifecycle_state() == voxel::ChunkLifecycleState::Generating) {
      (void)default_world_.set_chunk_lifecycle_state(
          pending.coord,
          voxel::ChunkLifecycleState::Requested);
    }

    pending_generation_jobs_.erase(
        pending_generation_jobs_.begin() + static_cast<std::ptrdiff_t>(index));
  }
}

void ServerRuntime::schedule_generation_jobs() {
  if (!chunk_generation_.has_value() || !chunk_generation_->enabled) {
    return;
  }

  const std::vector<ChunkCoord> candidates =
      chunk_streamer_.pending_generation_requests(default_world_);
  for (const ChunkCoord coord : candidates) {
    if (pending_generation_jobs_.size() >= max_pending_generation_jobs_) {
      return;
    }

    const bool already_pending = std::any_of(
        pending_generation_jobs_.begin(),
        pending_generation_jobs_.end(),
        [coord](const PendingGenerationJob& pending) {
          return pending.coord == coord;
        });
    if (already_pending) {
      continue;
    }

    voxel::ChunkSection* chunk = default_world_.find_chunk(coord);
    if (chunk == nullptr ||
        chunk->lifecycle_state() != voxel::ChunkLifecycleState::Requested) {
      continue;
    }

    if (!default_world_.set_chunk_lifecycle_state(
            coord,
            voxel::ChunkLifecycleState::Generating)) {
      continue;
    }

    world::ChunkGenerationRequest request;
    request.world_id = default_world_.id();
    request.seed = chunk_generation_->seed;
    request.coord = coord;
    request.config = chunk_generation_->config;
    request.palette = chunk_generation_->palette;
    auto job = world_jobs_.submit("server_world_generation", [request] {
      return world::generate_chunk(request);
    });
    pending_generation_jobs_.push_back({coord, std::move(job)});
  }
}

void ServerRuntime::drain_lighting_jobs() {
  for (usize index = 0; index < pending_lighting_jobs_.size();) {
    PendingLightingJob& pending = pending_lighting_jobs_[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    world::LightingResult result = pending.job.get();
    (void)default_world_.publish_lighting_result(std::move(result));
    pending_lighting_jobs_.erase(
        pending_lighting_jobs_.begin() + static_cast<std::ptrdiff_t>(index));
  }
}

void ServerRuntime::schedule_lighting_jobs() {
  if (block_registry_ == nullptr || !default_world_.has_lighting_work()) {
    return;
  }

  const world::LightingSnapshot snapshot = default_world_.make_lighting_snapshot();
  if (snapshot.chunks.empty()) {
    return;
  }

  const bool revision_in_flight = std::any_of(
      pending_lighting_jobs_.begin(),
      pending_lighting_jobs_.end(),
      [revision = snapshot.source_revision](const PendingLightingJob& pending) {
        return pending.source_revision == revision;
      });
  if (revision_in_flight || pending_lighting_jobs_.size() >= max_pending_lighting_jobs_) {
    return;
  }

  const voxel::BlockRegistry* registry = block_registry_;
  auto job = world_jobs_.submit("server_world_lighting", [snapshot, registry] {
    return world::WorldRuntime::build_lighting_result(snapshot, *registry);
  });
  pending_lighting_jobs_.push_back({snapshot.source_revision, std::move(job)});
}

void ServerRuntime::refresh_player_permissions(player::PlayerState& player_state) {
  const world::RegionQuery query{player_state.world_id, player_state.position};
  (void)default_world_.update_player_region_state(player_state.player_id, query);
  const player::PermissionLayer region_layer =
      default_world_.regions().permission_layer_at(query);

  player_state.resolved_permissions = player::resolve_permissions(
      game_modes_,
      permission_groups_,
      {
          player_state.game_mode_id,
          &player_state.permission_overrides,
          {},
          &region_layer,
          {},
      });
}

void ServerRuntime::apply_player_command(
    player::PlayerState& player_state,
    const player::PlayerCommand& command) {
  refresh_player_permissions(player_state);

  player_state.selected_hotbar_slot =
      player::is_valid_hotbar_slot(command.selected_hotbar_slot)
          ? command.selected_hotbar_slot
          : player_state.selected_hotbar_slot;

  player_state.yaw_radians += static_cast<f32>(command.look_delta_x) * player_tuning_.mouse_sensitivity_radians;
  player_state.pitch_radians = std::clamp(
      player_state.pitch_radians - static_cast<f32>(command.look_delta_y) * player_tuning_.mouse_sensitivity_radians,
      -player::DefaultMaxPitchRadians,
      player::DefaultMaxPitchRadians);

  if (!player_state.resolved_permissions.core.can_fly &&
      player_state.movement_mode == player::MovementMode::Flying) {
    player_state.movement_mode = player::MovementMode::Walking;
  }

  if (command.toggle_fly_pressed && player_state.resolved_permissions.core.can_fly) {
    player_state.movement_mode =
        player_state.movement_mode == player::MovementMode::Flying
            ? player::MovementMode::Walking
            : player::MovementMode::Flying;
  }

  world::WorldCollisionQuery collision_query(default_world_, *block_registry_, true);
  physics::KinematicConfig physics_config;
  physics_config.walk_speed = player_tuning_.walk_speed;
  physics_config.sprint_speed = player_tuning_.sprint_speed;
  physics_config.sneak_speed = player_tuning_.sneak_speed;
  physics_config.air_speed = player_tuning_.air_speed;
  physics_config.fly_speed = player_tuning_.fly_speed;
  physics_config.fly_vertical_speed = player_tuning_.fly_vertical_speed;
  physics_config.jump_velocity = player_tuning_.jump_velocity;
  physics_config.gravity_per_second = player_tuning_.gravity_per_second;
  physics_config.terminal_velocity = player_tuning_.terminal_velocity;
  physics_config.body_width = player_tuning_.body_width;
  physics_config.body_height = player_tuning_.body_height;
  physics_config.sneaking_body_height = player_tuning_.sneaking_body_height;
  physics_config.sneak_support_probe_depth = player_tuning_.sneak_support_probe_depth;

  physics::KinematicInput input;
  input.move_x = command.move_x;
  input.move_z = command.move_z;
  input.jump_pressed = command.jump_pressed;
  input.jump_held = command.jump_held;
  input.sneak_held = command.sneak_held;
  input.sprint_held = command.sprint_held;
  input.fly_mode =
      player_state.resolved_permissions.core.can_fly &&
      player_state.movement_mode == player::MovementMode::Flying;

  const Seconds tick_seconds = tick_duration_seconds(base_tps_);
  if (player_state.resolved_permissions.core.has_collision) {
    const physics::KinematicStepResult step = physics::step_kinematic_body(
        collision_query,
        {player_state.position, player_state.velocity, player_state.grounded, player_state.sneaking},
        player_state.yaw_radians,
        tick_seconds,
        input,
        physics_config);
    player_state.position = step.body.position;
    player_state.velocity = step.body.velocity;
    player_state.grounded = step.body.grounded;
    player_state.sneaking = step.body.sneaking;
  } else {
    step_player_without_collision(player_state, command, player_tuning_, tick_seconds);
  }

  refresh_player_permissions(player_state);

  world::WorldCollisionQuery interaction_query(default_world_, *block_registry_, false);
  const Vec3f eye_position = player_eye_position(player_state, player_tuning_);
  const Vec3f look_direction = player_look_direction(player_state);

  if (command.primary_pressed && player_state.resolved_permissions.core.can_modify_world) {
    auto hit = physics::raycast_blocks(
        interaction_query,
        eye_position,
        look_direction,
        player_tuning_.reach_distance);
    if (hit.has_value()) {
      // TODO(arvane): Replace instant-break interaction with documented timed
      // break progress once block hardness and tool rules are specified.
      if (!block_registry_->is_air(default_world_.get_block(hit->block))) {
        (void)default_world_.set_block(hit->block, voxel::AirBlockVariantID);
      }
    }
  }

  if (command.secondary_pressed && player_state.resolved_permissions.core.can_modify_world) {
    const std::optional<player::ItemStack>& held_slot =
        player_state.inventory.slot(player_state.selected_hotbar_slot);
    if (!held_slot.has_value()) {
      return;
    }

    const player::ItemRecord* held_item = item_registry_->item(held_slot->item);
    if (held_item == nullptr || !held_item->placement_variant.has_value()) {
      return;
    }

    auto hit = physics::raycast_blocks(
        interaction_query,
        eye_position,
        look_direction,
        player_tuning_.reach_distance);
    if (!hit.has_value()) {
      return;
    }

    BlockPos place_pos = hit->block;
    const voxel::BlockVariantID target_variant = default_world_.get_block(hit->block);
    if (!block_registry_->is_replaceable(target_variant)) {
      place_pos = offset_block(place_pos, hit->face);
    }

    if (!interaction_query.is_block_loaded(place_pos)) {
      return;
    }

    const voxel::BlockVariantID existing = default_world_.get_block(place_pos);
    if (!(block_registry_->is_air(existing) || block_registry_->is_replaceable(existing))) {
      return;
    }

    const voxel::BlockVariantID placed_variant = held_item->placement_variant.value();
    const physics::KinematicConfig placement_config{
        player_tuning_.walk_speed,
        player_tuning_.sprint_speed,
        player_tuning_.sneak_speed,
        player_tuning_.air_speed,
        player_tuning_.fly_speed,
        player_tuning_.fly_vertical_speed,
        player_tuning_.jump_velocity,
        player_tuning_.gravity_per_second,
        player_tuning_.terminal_velocity,
        player_tuning_.body_width,
        player_tuning_.body_height,
        player_tuning_.sneaking_body_height,
        player_tuning_.sneak_support_probe_depth};
    const Aabb3f player_bounds =
        physics::make_body_bounds(player_state.position, placement_config, player_state.sneaking);
    for (const Aabb3f& local_box : block_registry_->collision_boxes(placed_variant)) {
      Aabb3f world_box{
          {local_box.min.x + static_cast<f32>(place_pos.value.x),
           local_box.min.y + static_cast<f32>(place_pos.value.y),
           local_box.min.z + static_cast<f32>(place_pos.value.z)},
          {local_box.max.x + static_cast<f32>(place_pos.value.x),
           local_box.max.y + static_cast<f32>(place_pos.value.y),
           local_box.max.z + static_cast<f32>(place_pos.value.z)}};
      if (player_bounds.overlaps(world_box)) {
        return;
      }
    }

    if (default_world_.set_block(place_pos, placed_variant)) {
      (void)player_state.inventory.consume_one(player_state.selected_hotbar_slot);
    }
  }
}

void ServerRuntime::send_player_corrections() {
  for (const SessionID session_id : sessions_.session_ids()) {
    ServerSession* session = sessions_.find(session_id);
    if (session == nullptr) {
      continue;
    }

    const player::PlayerState* player_state = default_world_.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    protocol::PlayerCorrection correction = make_player_correction(tick_index_, *player_state);
    if (const auto found = last_sent_player_corrections_.find(player_state->player_id);
        found != last_sent_player_corrections_.end() &&
        same_correction_payload(found->second, correction)) {
      continue;
    }

    std::vector<u8> payload = protocol::serialize_player_correction(correction);
    std::vector<u8> packet = protocol::serialize_envelope(
        {protocol::CurrentProtocolVersion, protocol::MessageType::PlayerCorrection, 0, 0, 0, 0},
        payload);
    const net::NetworkResult send_result = transport_->send(
        {session->connection_id, protocol::ReliabilityClass::UnreliableLatestWins, std::move(packet)});
    if (send_result) {
      last_sent_player_corrections_[player_state->player_id] = std::move(correction);
    }
  }
}

void ServerRuntime::seed_player_inventory(player::PlayerState& player_state) {
  if (!inventory_is_empty(player_state.inventory)) {
    return;
  }

  // TODO(arvane): Replace this starter loadout with documented gameplay spawn
  // inventory rules once the built-in game layer is defined.
  for (const auto [item_name, count] :
       {std::pair{std::string_view{"arvane:stone"}, u16{64}},
        std::pair{std::string_view{"arvane:dirt"}, u16{64}},
        std::pair{std::string_view{"arvane:grass_block"}, u16{64}},
        std::pair{std::string_view{"arvane:glass"}, u16{64}},
        std::pair{std::string_view{"arvane:glowstone"}, u16{64}},
        std::pair{std::string_view{"arvane:coal"}, u16{32}},
        std::pair{std::string_view{"arvane:wood_pickaxe"}, u16{1}}}) {
    const auto item_id = item_registry_->find_item(item_name);
    if (!item_id.has_value()) {
      continue;
    }
    (void)player_state.inventory.insert_stack({item_id.value(), count}, *item_registry_);
  }
}

bool ServerRuntime::inventory_is_empty(const player::Inventory& inventory) const noexcept {
  for (usize slot_index = 0; slot_index < inventory.slot_count(); ++slot_index) {
    if (inventory.slot(slot_index).has_value()) {
      return false;
    }
  }
  return true;
}

} // namespace arv::server
