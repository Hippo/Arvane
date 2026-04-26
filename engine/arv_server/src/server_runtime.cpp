#include "arv/server/server_runtime.hpp"

#include "arv/physics/collision.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
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

[[nodiscard]] arv::Vec3f player_eye_position(
    const player::PlayerState& player_state,
    const player::PlayerTuning& tuning) noexcept {
  return {
      player_state.position.x,
      player_state.position.y + tuning.standing_eye_height,
      player_state.position.z};
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

  inbound_events_ = transport_->poll_events();
  for (net::NetworkEvent& event : inbound_events_) {
    handle_network_event(std::move(event));
  }
  tick_players();
  (void)default_world_.tick(*block_registry_, fluid_definition_);
  send_player_corrections();
  ++tick_index_;
}

void ServerRuntime::shutdown() {
  if (state_ == ServerRuntimeState::Stopped) {
    return;
  }

  state_ = ServerRuntimeState::Stopping;
  if (transport_) {
    transport_->shutdown();
  }
  state_ = ServerRuntimeState::Stopped;
}

void ServerRuntime::handle_network_event(net::NetworkEvent event) {
  switch (event.type) {
    case net::NetworkEventType::ConnectionRequested:
    case net::NetworkEventType::ConnectionAccepted:
      return;
    case net::NetworkEventType::ConnectionClosed:
    case net::NetworkEventType::ConnectionFailed:
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

void ServerRuntime::apply_player_command(
    player::PlayerState& player_state,
    const player::PlayerCommand& command) {
  player_state.selected_hotbar_slot =
      player::is_valid_hotbar_slot(command.selected_hotbar_slot)
          ? command.selected_hotbar_slot
          : player_state.selected_hotbar_slot;

  player_state.yaw_radians += static_cast<f32>(command.look_delta_x) * player_tuning_.mouse_sensitivity_radians;
  player_state.pitch_radians = std::clamp(
      player_state.pitch_radians - static_cast<f32>(command.look_delta_y) * player_tuning_.mouse_sensitivity_radians,
      -player::DefaultMaxPitchRadians,
      player::DefaultMaxPitchRadians);

  if (command.toggle_fly_pressed) {
    player_state.movement_mode =
        player_state.movement_mode == player::MovementMode::Flying
            ? player::MovementMode::Walking
            : player::MovementMode::Flying;
  }

  world::WorldCollisionQuery collision_query(default_world_, *block_registry_, true);
  physics::KinematicConfig physics_config;
  physics_config.walk_speed = player_tuning_.walk_speed;
  physics_config.sprint_speed = player_tuning_.sprint_speed;
  physics_config.air_speed = player_tuning_.air_speed;
  physics_config.fly_speed = player_tuning_.fly_speed;
  physics_config.fly_vertical_speed = player_tuning_.fly_vertical_speed;
  physics_config.jump_velocity = player_tuning_.jump_velocity;
  physics_config.gravity_per_second = player_tuning_.gravity_per_second;
  physics_config.terminal_velocity = player_tuning_.terminal_velocity;
  physics_config.body_width = player_tuning_.body_width;
  physics_config.body_height = player_tuning_.body_height;

  physics::KinematicInput input;
  input.move_x = command.move_x;
  input.move_z = command.move_z;
  input.jump_pressed = command.jump_pressed;
  input.jump_held = command.jump_held;
  input.move_down_held = command.move_down_held;
  input.sprint_held = command.sprint_held;
  input.fly_mode = player_state.movement_mode == player::MovementMode::Flying;

  const physics::KinematicStepResult step = physics::step_kinematic_body(
      collision_query,
      {player_state.position, player_state.velocity, player_state.grounded},
      player_state.yaw_radians,
      tick_duration_seconds(base_tps_),
      input,
      physics_config);
  player_state.position = step.body.position;
  player_state.velocity = step.body.velocity;
  player_state.grounded = step.body.grounded;

  world::WorldCollisionQuery interaction_query(default_world_, *block_registry_, false);
  const Vec3f eye_position = player_eye_position(player_state, player_tuning_);
  const Vec3f look_direction = player_look_direction(player_state);

  if (command.primary_pressed) {
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

  if (command.secondary_pressed) {
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
        player_tuning_.air_speed,
        player_tuning_.fly_speed,
        player_tuning_.fly_vertical_speed,
        player_tuning_.jump_velocity,
        player_tuning_.gravity_per_second,
        player_tuning_.terminal_velocity,
        player_tuning_.body_width,
        player_tuning_.body_height};
    const Aabb3f player_bounds = physics::make_body_bounds(player_state.position, placement_config);
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

    protocol::PlayerCorrection correction;
    correction.server_tick = tick_index_;
    correction.player_id = player_state->player_id;
    correction.world_id = player_state->world_id;
    correction.position = player_state->position;
    correction.velocity = player_state->velocity;
    correction.yaw_radians = player_state->yaw_radians;
    correction.pitch_radians = player_state->pitch_radians;
    correction.movement_mode = player_state->movement_mode;
    correction.grounded = player_state->grounded;
    correction.selected_hotbar_slot = player_state->selected_hotbar_slot;

    std::vector<u8> payload = protocol::serialize_player_correction(correction);
    std::vector<u8> packet = protocol::serialize_envelope(
        {protocol::CurrentProtocolVersion, protocol::MessageType::PlayerCorrection, 0, 0, 0, 0},
        payload);
    (void)transport_->send(
        {session->connection_id, protocol::ReliabilityClass::UnreliableLatestWins, std::move(packet)});
  }
}

void ServerRuntime::seed_player_inventory(player::PlayerState& player_state) {
  if (!inventory_is_empty(player_state.inventory)) {
    return;
  }

  // TODO(arvane): Replace this starter loadout with documented gameplay spawn
  // inventory rules once the built-in game layer is defined.
  for (const std::string_view item_name :
       {std::string_view{"arvane:stone"},
        std::string_view{"arvane:dirt"},
        std::string_view{"arvane:grass_block"},
        std::string_view{"arvane:glass"},
        std::string_view{"arvane:glowstone"}}) {
    const auto item_id = item_registry_->find_item(item_name);
    if (!item_id.has_value()) {
      continue;
    }
    (void)player_state.inventory.insert_stack({item_id.value(), 64U}, *item_registry_);
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
