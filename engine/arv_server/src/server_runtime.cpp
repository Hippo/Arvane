#include "arv/server/server_runtime.hpp"

#include "arv/core/perf_stats.hpp"
#include "arv/core/profiling.hpp"

#include "arv/physics/collision.hpp"
#include "arv/protocol/message_envelope.hpp"
#include "arv/world/world_queries.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
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
      player_state.swimming
          ? tuning.swimming_eye_height
          : (player_state.sneaking ? tuning.sneaking_eye_height
                                   : tuning.standing_eye_height);
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
  player_state.swimming = false;
  player_state.breath_seconds = tuning.breath_seconds;
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

[[nodiscard]] arv::i32 floor_i32(arv::f32 value) noexcept {
  return static_cast<arv::i32>(std::floor(value));
}

[[nodiscard]] bool block_stops_bucket_raycast(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    BlockPos pos) noexcept {
  const voxel::BlockVariantID block = world.get_block(pos);
  const voxel::BlockVariantRecord* variant = registry.variant(block);
  return variant != nullptr && !registry.is_air(block) && !variant->properties.replaceable;
}

[[nodiscard]] std::optional<BlockPos> raycast_source_fluid(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    Vec3f origin,
    Vec3f direction,
    f32 max_distance) {
  direction = normalize_or_zero(direction);
  if (direction == Vec3f{}) {
    return std::nullopt;
  }

  i32 x = floor_i32(origin.x);
  i32 y = floor_i32(origin.y);
  i32 z = floor_i32(origin.z);

  const i32 step_x = direction.x > 0.0F ? 1 : (direction.x < 0.0F ? -1 : 0);
  const i32 step_y = direction.y > 0.0F ? 1 : (direction.y < 0.0F ? -1 : 0);
  const i32 step_z = direction.z > 0.0F ? 1 : (direction.z < 0.0F ? -1 : 0);

  const f32 inf = std::numeric_limits<f32>::infinity();
  auto initial_t_max = [&](f32 origin_axis, f32 direction_axis, i32 cell, i32 step) -> f32 {
    if (step == 0) {
      return inf;
    }
    const f32 boundary = step > 0 ? static_cast<f32>(cell + 1) : static_cast<f32>(cell);
    return (boundary - origin_axis) / direction_axis;
  };

  f32 t_max_x = initial_t_max(origin.x, direction.x, x, step_x);
  f32 t_max_y = initial_t_max(origin.y, direction.y, y, step_y);
  f32 t_max_z = initial_t_max(origin.z, direction.z, z, step_z);
  const f32 t_delta_x = step_x == 0 ? inf : std::abs(1.0F / direction.x);
  const f32 t_delta_y = step_y == 0 ? inf : std::abs(1.0F / direction.y);
  const f32 t_delta_z = step_z == 0 ? inf : std::abs(1.0F / direction.z);

  constexpr usize MaxIterations = 2048;
  for (usize iteration = 0; iteration < MaxIterations; ++iteration) {
    const BlockPos block{{x, y, z}};
    if (!world.find_chunk(block_to_chunk(block))) {
      return std::nullopt;
    }

    const voxel::FluidCell fluid = world.get_fluid(block);
    if (fluid.type == definition.type && fluid.source()) {
      return block;
    }
    if (block_stops_bucket_raycast(world, registry, block)) {
      return std::nullopt;
    }

    const f32 next_t = std::min(t_max_x, std::min(t_max_y, t_max_z));
    if (next_t > max_distance) {
      break;
    }

    if (t_max_x <= t_max_y && t_max_x <= t_max_z) {
      x += step_x;
      t_max_x += t_delta_x;
    } else if (t_max_y <= t_max_z) {
      y += step_y;
      t_max_y += t_delta_y;
    } else {
      z += step_z;
      t_max_z += t_delta_z;
    }
  }

  return std::nullopt;
}

[[nodiscard]] std::optional<player::ItemID> find_item_id(
    const player::ItemRegistry& registry,
    std::string_view name) {
  return registry.find_item(name);
}

[[nodiscard]] std::optional<player::Inventory> inventory_after_bucket_fill(
    const player::PlayerState& player_state,
    const player::ItemRegistry& item_registry) {
  const std::optional<player::ItemID> water_bucket =
      find_item_id(item_registry, "arvane:water_bucket");
  if (!water_bucket.has_value()) {
    return std::nullopt;
  }

  player::Inventory next_inventory = player_state.inventory;
  const usize slot = player_state.selected_hotbar_slot;
  const std::optional<player::ItemStack>& held = next_inventory.slot(slot);
  if (!held.has_value()) {
    return std::nullopt;
  }

  if (held->count == 1U) {
    if (!next_inventory.set_slot(slot, player::ItemStack{water_bucket.value(), 1U})) {
      return std::nullopt;
    }
  } else {
    if (!next_inventory.consume_one(slot)) {
      return std::nullopt;
    }
    const player::InventoryInsertResult insert =
        next_inventory.insert_stack({water_bucket.value(), 1U}, item_registry);
    if (!insert.complete()) {
      return std::nullopt;
    }
  }

  return next_inventory;
}

[[nodiscard]] std::optional<player::Inventory> inventory_after_water_place(
    const player::PlayerState& player_state,
    const player::ItemRegistry& item_registry) {
  const std::optional<player::ItemID> bucket = find_item_id(item_registry, "arvane:bucket");
  if (!bucket.has_value()) {
    return std::nullopt;
  }

  player::Inventory next_inventory = player_state.inventory;
  const usize slot = player_state.selected_hotbar_slot;
  const std::optional<player::ItemStack>& held = next_inventory.slot(slot);
  if (!held.has_value()) {
    return std::nullopt;
  }

  if (held->count == 1U) {
    if (!next_inventory.set_slot(slot, player::ItemStack{bucket.value(), 1U})) {
      return std::nullopt;
    }
  } else {
    if (!next_inventory.consume_one(slot)) {
      return std::nullopt;
    }
    const player::InventoryInsertResult insert =
        next_inventory.insert_stack({bucket.value(), 1U}, item_registry);
    if (!insert.complete()) {
      return std::nullopt;
    }
  }

  return next_inventory;
}

[[nodiscard]] bool can_place_fluid_source_at(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry& registry,
    const voxel::FluidDefinition& definition,
    BlockPos pos) noexcept {
  const voxel::BlockVariantID existing_block = world.get_block(pos);
  if (!(registry.is_air(existing_block) || registry.is_replaceable(existing_block))) {
    return false;
  }

  const voxel::FluidCell existing_fluid = world.get_fluid(pos);
  return existing_fluid.empty() ||
         (existing_fluid.type == definition.type && !existing_fluid.source());
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
  correction.swimming = player_state.swimming;
  correction.breath_seconds = player_state.breath_seconds;
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
         lhs.swimming == rhs.swimming &&
         lhs.breath_seconds == rhs.breath_seconds &&
         lhs.selected_hotbar_slot == rhs.selected_hotbar_slot;
}

} // namespace

ServerRuntime::ServerRuntime(std::unique_ptr<net::ITransport> transport) : transport_(std::move(transport)),
                                                                           world_jobs_(std::make_unique<sched::Scheduler>()) {
}

net::NetworkResult ServerRuntime::start(const ServerStartInfo& start_info) {
  if (!transport_) {
    return net::NetworkResult::failure(net::NetworkError{"server has no transport"});
  }

  mode_ = start_info.mode;
  base_tps_ = start_info.base_tps;
  block_registry_ = start_info.block_registry;
  item_registry_ = start_info.item_registry;
  entity_registry_ = start_info.entity_registry;
  player_tuning_ = start_info.player_tuning;
  fluid_definition_ = start_info.fluid_definition;
  chunk_streamer_.configure(start_info.chunk_streaming);
  const i32 max_view_distance = std::max<i32>(start_info.chunk_streaming.max_view_distance, 0);
  simulation_radius_ = std::clamp(
      start_info.chunk_streaming.simulation_distance,
      0,
      max_view_distance);
  chunk_generation_ = start_info.chunk_generation;
  if (chunk_generation_.has_value() && chunk_generation_->enabled) {
    default_world_.set_generation_context(
        chunk_generation_->seed,
        chunk_generation_->config);
  } else {
    default_world_.clear_generation_context();
  }
  max_pending_generation_jobs_ =
      std::max<u32>(start_info.max_pending_generation_jobs, 1U);
  max_generation_results_per_tick_ =
      std::max<u32>(start_info.max_generation_results_per_tick, 1U);
  max_lighting_results_per_tick_ =
      std::max<u32>(start_info.max_lighting_results_per_tick, 1U);
  max_fluid_results_per_tick_ =
      std::max<u32>(start_info.max_fluid_results_per_tick, 1U);
  world_jobs_ = std::make_unique<sched::Scheduler>(start_info.worker_threads);
  max_pending_fluid_jobs_ = std::max<u32>(start_info.max_pending_fluid_jobs, 1U);
  fluid_update_interval_ = start_info.world_tick_policy.fluid_interval;
  fluid_update_budget_ = start_info.world_tick_policy.fluid_budget == 0U
                             ? world::WorldTickPolicy{}.fluid_budget
                             : start_info.world_tick_policy.fluid_budget;
  world::WorldTickPolicy tick_policy = start_info.world_tick_policy;
  tick_policy.fluid_budget = 0U;
  default_world_.set_tick_policy(tick_policy);
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

  drain_generation_jobs(false);
  drain_fluid_jobs(false);
  drain_lighting_jobs(false);
  inbound_events_ = transport_->poll_events();
  for (net::NetworkEvent& event : inbound_events_) {
    handle_network_event(std::move(event));
  }
  tick_players();
  update_world_simulation_activity();
  (void)default_world_.tick(*block_registry_, fluid_definition_);
  schedule_fluid_jobs();
  schedule_lighting_jobs();
  tick_chunk_streaming();
  schedule_generation_jobs();
  send_player_corrections();
  send_block_updates();
  send_entity_replication();
  send_inventory_updates();
  ++tick_index_;
}

void ServerRuntime::shutdown() {
  if (state_ == ServerRuntimeState::Stopped) {
    return;
  }

  state_ = ServerRuntimeState::Stopping;
  if (world_jobs_) {
    world_jobs_->wait_for_all();
  }
  drain_generation_jobs(true);
  drain_fluid_jobs(true);
  drain_lighting_jobs(true);
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

player::InventoryInsertResult ServerRuntime::insert_player_inventory_stack(
    PlayerID player_id,
    player::ItemStack stack,
    std::optional<usize> preferred_slot,
    bool replace_preferred_slot) {
  player::InventoryInsertResult result;
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || item_registry_ == nullptr || stack.count == 0U ||
      item_registry_->item(stack.item) == nullptr) {
    result.leftover = stack;
    return result;
  }

  if (preferred_slot.has_value()) {
    if (!player_state->inventory.is_valid_slot(preferred_slot.value())) {
      result.leftover = stack;
      return result;
    }

    if (replace_preferred_slot) {
      player_state->inventory.set_slot(preferred_slot.value(), stack);
      return result;
    }

    player::ItemStack remaining = stack;
    (void)player_state->inventory.merge_stack_into_slot(
        preferred_slot.value(),
        remaining,
        *item_registry_);
    if (remaining.count == 0U) {
      return result;
    }
    stack = remaining;
  }

  return player_state->inventory.insert_stack(stack, *item_registry_);
}

player::InventoryOperationResult ServerRuntime::apply_player_inventory_click(
    PlayerID player_id,
    usize slot_index,
    player::InventoryClickButton button) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || item_registry_ == nullptr) {
    return {player::InventoryOperationResultKind::RejectedInvalidSlot};
  }

  return player::apply_inventory_slot_click(
      player_state->inventory,
      slot_index,
      button,
      player_state->inventory_cursor_stack,
      *item_registry_);
}

std::optional<player::ItemStack> ServerRuntime::drop_player_inventory_cursor(PlayerID player_id) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || !player_state->inventory_cursor_stack.has_value()) {
    return std::nullopt;
  }

  std::optional<player::ItemStack> dropped = player_state->inventory_cursor_stack;
  dropped_item_stacks_.emplace_back(
      player_state->world_id,
      player_state->position,
      dropped.value());
  player_state->inventory_cursor_stack.reset();
  return dropped;
}

std::optional<player::ItemStack> ServerRuntime::drop_one_from_player_inventory_cursor(
    PlayerID player_id) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || !player_state->inventory_cursor_stack.has_value() ||
      player_state->inventory_cursor_stack->count == 0U) {
    return std::nullopt;
  }

  player::ItemStack dropped{
      player_state->inventory_cursor_stack->item,
      1U};
  dropped_item_stacks_.emplace_back(
      player_state->world_id,
      player_state->position,
      dropped);
  player_state->inventory_cursor_stack->count =
      static_cast<u16>(player_state->inventory_cursor_stack->count - 1U);
  if (player_state->inventory_cursor_stack->count == 0U) {
    player_state->inventory_cursor_stack.reset();
  }
  return dropped;
}

player::InventoryOperationResult ServerRuntime::close_player_inventory_menu(PlayerID player_id) {
  player::PlayerState* player_state = default_world_.find_player(player_id);
  if (player_state == nullptr || item_registry_ == nullptr) {
    return {player::InventoryOperationResultKind::RejectedInvalidSlot};
  }
  if (!player_state->inventory_cursor_stack.has_value()) {
    return {player::InventoryOperationResultKind::NoOp};
  }

  player::ItemStack carried = player_state->inventory_cursor_stack.value();
  const player::InventoryInsertResult insert_result =
      player_state->inventory.insert_stack(carried, *item_registry_);
  if (insert_result.complete()) {
    player_state->inventory_cursor_stack.reset();
    return {player::InventoryOperationResultKind::Success, carried.count, carried};
  }

  player_state->inventory_cursor_stack = insert_result.leftover;
  return {
      insert_result.leftover->count == carried.count
          ? player::InventoryOperationResultKind::RejectedFullInventory
          : player::InventoryOperationResultKind::PartialSuccess,
      static_cast<u16>(carried.count - insert_result.leftover->count),
      insert_result.leftover};
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
        entity_replication_.erase(session->session_id);
        inventory_replication_.erase(session->player_id);
        pending_player_commands_.erase(session->player_id);
        last_sent_player_corrections_.erase(session->player_id);
        if (const std::optional<EntityID> entity_id =
                default_world_.entity_for_player(session->player_id);
            entity_id.has_value()) {
          (void)default_world_.destroy_entity(entity_id.value(), world::EntityDespawnReason::Removed);
          default_world_.flush_entity_despawns();
        }
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

      // TODO(arvane): Validate real authentication/session tokens once the
      // account/session authority is specified.
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
        session.connection_state = net::ConnectionState::WorldJoin;
      }

      std::vector<u8> payload = protocol::serialize_server_hello(server_hello);
      std::vector<u8> reply = protocol::serialize_envelope(
          {protocol::CurrentProtocolVersion, protocol::MessageType::ServerHello, 0, 0, 0, 0},
          payload);
      (void)transport_->send({connection_id, protocol::ReliabilityClass::ReliableOrdered, std::move(reply)});
      if (server_hello.accepted) {
        if (ServerSession* session = sessions_.find_by_connection(connection_id);
            session != nullptr) {
          if (const player::PlayerState* player_state =
                  default_world_.find_player(session->player_id);
              player_state != nullptr) {
            send_world_join_result(*session, *player_state);
            session->connection_state = net::ConnectionState::InGame;
          }
        }
      }
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
    case protocol::MessageType::InventoryActionRequest: {
      auto request = protocol::deserialize_inventory_action_request(decoded.value().payload);
      if (!request) {
        return;
      }
      handle_inventory_action_request(connection_id, request.value());
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

void ServerRuntime::update_world_simulation_activity() {
  std::vector<ChunkCoord> centers;
  centers.reserve(sessions_.size());
  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr || session->world_id != default_world_.id()) {
      continue;
    }

    const player::PlayerState* player_state =
        default_world_.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    centers.emplace_back(player_chunk_coord(*player_state));
  }

  default_world_.update_simulation_radii(centers, simulation_radius_);
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

void ServerRuntime::handle_inventory_action_request(
    net::ConnectionID connection_id,
    const protocol::InventoryActionRequest& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || request.inventory_id != protocol::PlayerInventoryID) {
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

void ServerRuntime::drain_generation_jobs(bool drain_all) {
  ARV_PROFILE_ZONE("Chunk generation");
  u32 drained = 0;
  for (usize index = 0;
       index < pending_generation_jobs_.size() &&
       (drain_all || drained < max_generation_results_per_tick_);) {
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
        pending_generation_jobs_.begin() + static_cast<isize>(index));
    ++drained;
  }
}

void ServerRuntime::schedule_generation_jobs() {
  ARV_PROFILE_ZONE("Chunk generation");
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
    auto job = world_jobs_->submit("server_world_generation", [request = std::move(request)] {
      return world::generate_chunk(request);
    });
    pending_generation_jobs_.emplace_back(coord, std::move(job));
  }
}

void ServerRuntime::drain_lighting_jobs(bool drain_all) {
  ARV_PROFILE_ZONE("Lighting propagation");
  u32 drained = 0;
  for (usize index = 0;
       index < pending_lighting_jobs_.size() &&
       (drain_all || drained < max_lighting_results_per_tick_);) {
    PendingLightingJob& pending = pending_lighting_jobs_[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    world::LightingResult result = pending.job.get();
    (void)default_world_.publish_lighting_result(std::move(result));
    pending_lighting_jobs_.erase(
        pending_lighting_jobs_.begin() + static_cast<isize>(index));
    ++drained;
  }
}

void ServerRuntime::drain_fluid_jobs(bool drain_all) {
  u32 drained = 0;
  for (usize index = 0;
       index < pending_fluid_jobs_.size() &&
       (drain_all || drained < max_fluid_results_per_tick_);) {
    PendingFluidJob& pending = pending_fluid_jobs_[index];
    if (!pending.job.is_complete()) {
      ++index;
      continue;
    }

    world::FluidSimulationResult result = pending.job.get();
    (void)default_world_.apply_fluid_simulation_result(
        std::move(result),
        fluid_definition_);
    pending_fluid_jobs_.erase(
        pending_fluid_jobs_.begin() + static_cast<isize>(index));
    ++drained;
  }
}

void ServerRuntime::schedule_fluid_jobs() {
  if (block_registry_ == nullptr ||
      pending_fluid_jobs_.size() >= max_pending_fluid_jobs_) {
    return;
  }

  if (fluid_update_interval_ != 0U &&
      default_world_.tick_index() % fluid_update_interval_ != 0U) {
    return;
  }
  std::optional<world::FluidSimulationRequest> request =
      default_world_.make_fluid_simulation_request(fluid_update_budget_);
  if (!request.has_value()) {
    return;
  }

  const voxel::BlockRegistry* registry = block_registry_;
  const voxel::FluidDefinition definition = fluid_definition_;
  auto job = world_jobs_->submit(
      "server_world_fluids",
      [request = std::move(request.value()), registry, definition]() mutable {
        return world::FluidSolver::build_simulation_result(
            std::move(request),
            *registry,
            definition);
      });
  pending_fluid_jobs_.emplace_back(std::move(job));
}

void ServerRuntime::schedule_lighting_jobs() {
  ARV_PROFILE_ZONE("Lighting propagation");
  if (block_registry_ == nullptr || !default_world_.has_lighting_work()) {
    return;
  }

  world::LightingSnapshot snapshot = default_world_.make_lighting_snapshot();
  if (snapshot.chunks.empty()) {
    return;
  }
  const u64 source_revision = snapshot.source_revision;

  const bool revision_in_flight = std::any_of(
      pending_lighting_jobs_.begin(),
      pending_lighting_jobs_.end(),
      [revision = source_revision](const PendingLightingJob& pending) {
        return pending.source_revision == revision;
      });
  if (revision_in_flight || pending_lighting_jobs_.size() >= max_pending_lighting_jobs_) {
    return;
  }

  const voxel::BlockRegistry* registry = block_registry_;
  core::record_lighting_job_submitted();
  auto job = world_jobs_->submit("server_world_lighting", [snapshot = std::move(snapshot), registry] {
    return world::WorldRuntime::build_lighting_result(snapshot, *registry);
  });
  pending_lighting_jobs_.emplace_back(source_revision, std::move(job));
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
  physics_config.standing_eye_height = player_tuning_.standing_eye_height;
  physics_config.sneaking_eye_height = player_tuning_.sneaking_eye_height;
  physics_config.swimming_eye_height = player_tuning_.swimming_eye_height;
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
  physics_config.swimming_body_width = player_tuning_.swimming_body_width;
  physics_config.swimming_body_height = player_tuning_.swimming_body_height;
  physics_config.sneak_support_probe_depth = player_tuning_.sneak_support_probe_depth;
  physics_config.collision_epsilon = player_tuning_.collision_epsilon;
  physics_config.surface_swim_speed = player_tuning_.surface_swim_speed;
  physics_config.underwater_swim_speed = player_tuning_.underwater_swim_speed;
  physics_config.fast_surface_swim_speed = player_tuning_.fast_surface_swim_speed;
  physics_config.fast_underwater_swim_speed = player_tuning_.fast_underwater_swim_speed;
  physics_config.upward_swim_acceleration = player_tuning_.upward_swim_acceleration;
  physics_config.downward_swim_acceleration = player_tuning_.downward_swim_acceleration;
  physics_config.liquid_drag = player_tuning_.liquid_drag;
  physics_config.surface_float_depth = player_tuning_.surface_float_depth;
  physics_config.surface_buoyancy = player_tuning_.surface_buoyancy;
  physics_config.underwater_buoyancy = player_tuning_.underwater_buoyancy;
  physics_config.breath_seconds = player_tuning_.breath_seconds;

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
  input.pitch_radians = player_state.pitch_radians;

  const Seconds tick_seconds = tick_duration_seconds(base_tps_);
  if (player_state.resolved_permissions.core.has_collision) {
    const physics::KinematicStepResult step = physics::step_kinematic_body(
        collision_query,
        {player_state.position,
         player_state.velocity,
         player_state.grounded,
         player_state.sneaking,
         player_state.swimming,
         player_state.breath_seconds},
        player_state.yaw_radians,
        tick_seconds,
        input,
        physics_config);
    player_state.position = step.body.position;
    player_state.velocity = step.body.velocity;
    player_state.grounded = step.body.grounded;
    player_state.sneaking = step.body.sneaking;
    player_state.swimming = step.body.swimming;
    player_state.breath_seconds = step.body.breath_seconds;
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
    if (held_item == nullptr) {
      return;
    }

    if (held_item->name == "arvane:bucket") {
      const std::optional<BlockPos> source = raycast_source_fluid(
          default_world_,
          *block_registry_,
          fluid_definition_,
          eye_position,
          look_direction,
          player_tuning_.reach_distance);
      if (!source.has_value()) {
        return;
      }
      std::optional<player::Inventory> next_inventory =
          inventory_after_bucket_fill(player_state, *item_registry_);
      if (!next_inventory.has_value()) {
        return;
      }
      if (default_world_.set_fluid(
              source.value(),
              voxel::empty_fluid_cell(),
              fluid_definition_)) {
        player_state.inventory = std::move(next_inventory.value());
      }
      return;
    }

    if (held_item->name == "arvane:water_bucket") {
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

      if (!can_place_fluid_source_at(
              default_world_,
              *block_registry_,
              fluid_definition_,
              place_pos)) {
        return;
      }

      std::optional<player::Inventory> next_inventory =
          inventory_after_water_place(player_state, *item_registry_);
      if (!next_inventory.has_value()) {
        return;
      }
      if (default_world_.place_fluid_source(place_pos, fluid_definition_)) {
        player_state.inventory = std::move(next_inventory.value());
      }
      return;
    }

    if (!held_item->placement_variant.has_value()) {
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
    const physics::PlayerCollisionPose player_pose =
        player_state.swimming
            ? physics::PlayerCollisionPose::Swimming
            : (player_state.sneaking ? physics::PlayerCollisionPose::Sneaking
                                     : physics::PlayerCollisionPose::Standing);
    const Aabb3f player_bounds =
        physics::make_body_bounds(player_state.position, physics_config, player_pose);
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

void ServerRuntime::send_world_join_result(
    const ServerSession& session,
    const player::PlayerState& player_state) {
  protocol::WorldJoinResult result;
  result.accepted = true;
  result.world_id = session.world_id;
  result.player_id = session.player_id;
  result.spawn_position = player_state.position;
  result.initial_tick = tick_index_;
  result.tick_rate = base_tps_;
  result.effective_view_distance = 0;
  if (const std::optional<EntityID> entity_id =
          default_world_.entity_for_player(session.player_id);
      entity_id.has_value()) {
    result.local_entity_id =
        default_world_.network_id_for_entity(entity_id.value()).value_or(EntityNetID{});
  }

  const std::vector<u8> payload = protocol::serialize_world_join_result(result);
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, protocol::MessageType::WorldJoinResult, 0, 0, 0, 0},
      payload);
  (void)transport_->send(
      {session.connection_id, protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
}

void ServerRuntime::send_block_updates() {
  std::vector<world::ReplicatedBlockUpdate> updates =
      default_world_.drain_replicated_block_updates();
  if (updates.empty()) {
    return;
  }

  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr || session->world_id != default_world_.id()) {
      continue;
    }

    protocol::BlockUpdateBatch batch;
    batch.world_id = session->world_id;
    batch.server_tick = tick_index_;
    for (const world::ReplicatedBlockUpdate& update : updates) {
      if (!chunk_streamer_.session_has_sent_chunk(
              session_id,
              session->world_id,
              block_to_chunk(update.pos))) {
        continue;
      }
      batch.updates.push_back({
          update.pos,
          update.new_state,
          0U,
          update.server_tick});
    }
    if (batch.updates.empty()) {
      continue;
    }

    const std::vector<u8> payload = protocol::serialize_block_update_batch(batch);
    std::vector<u8> packet = protocol::serialize_envelope(
        {protocol::CurrentProtocolVersion, protocol::MessageType::BlockUpdateBatch, 0, 0, 0, 0},
        payload);
    const net::NetworkResult send_result = transport_->send(
        {session->connection_id, protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
    if (send_result) {
      for (const protocol::BlockUpdate& update : batch.updates) {
        const ChunkCoord coord = block_to_chunk(update.pos);
        if (const voxel::ChunkSection* chunk = default_world_.find_chunk(coord);
            chunk != nullptr) {
          (void)chunk_streamer_.mark_sent_block_version(
              session_id,
              session->world_id,
              coord,
              chunk->block_version());
        }
      }
    }
  }
}

void ServerRuntime::send_entity_replication() {
  const std::vector<entity::EntitySnapshotEntry> snapshots =
      default_world_.collect_entity_snapshots();

  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr || session->world_id != default_world_.id()) {
      continue;
    }

    EntityReplicationState& state = entity_replication_[session_id];
    HashSet<EntityNetID> relevant_now;
    std::vector<protocol::EntitySnapshotEntry> snapshot_entries;

    for (const entity::EntitySnapshotEntry& snapshot : snapshots) {
      const bool owns_entity =
          snapshot.owning_player.has_value() &&
          snapshot.owning_player.value() == session->player_id;
      const bool chunk_visible = chunk_streamer_.session_has_sent_chunk(
          session_id,
          session->world_id,
          snapshot.chunk);
      if (!owns_entity && !chunk_visible) {
        continue;
      }

      relevant_now.insert(snapshot.net_id);
      if (state.known_entities.find(snapshot.net_id) == state.known_entities.end()) {
        protocol::EntitySpawn spawn;
        spawn.world_id = session->world_id;
        spawn.entity_id = snapshot.net_id;
        spawn.entity_type = snapshot.type_id;
        spawn.entity_type_name = snapshot.type_name;
        spawn.position = snapshot.position;
        spawn.velocity = snapshot.velocity;
        spawn.rotation = snapshot.rotation;
        spawn.owning_player = snapshot.owning_player;
        const std::vector<u8> payload = protocol::serialize_entity_spawn(spawn);
        std::vector<u8> packet = protocol::serialize_envelope(
            {protocol::CurrentProtocolVersion, protocol::MessageType::EntitySpawn, 0, 0, 0, 0},
            payload);
        (void)transport_->send(
            {session->connection_id, protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
      }

      snapshot_entries.push_back({
          snapshot.net_id,
          snapshot.position,
          snapshot.velocity,
          snapshot.rotation,
          0U});
    }

    for (const EntityNetID known_entity : state.known_entities) {
      if (relevant_now.find(known_entity) != relevant_now.end()) {
        continue;
      }

      protocol::EntityDespawn despawn{session->world_id, known_entity, 0U};
      const std::vector<u8> payload = protocol::serialize_entity_despawn(despawn);
      std::vector<u8> packet = protocol::serialize_envelope(
          {protocol::CurrentProtocolVersion, protocol::MessageType::EntityDespawn, 0, 0, 0, 0},
          payload);
      (void)transport_->send(
          {session->connection_id, protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
    }

    state.known_entities = std::move(relevant_now);
    if (snapshot_entries.empty()) {
      continue;
    }

    // TODO(arvane): Replace full snapshot entries with per-component delta
    // compression once component replication masks are specified.
    protocol::EntitySnapshot snapshot_message;
    snapshot_message.world_id = session->world_id;
    snapshot_message.server_tick = tick_index_;
    snapshot_message.entities = std::move(snapshot_entries);
    const std::vector<u8> payload = protocol::serialize_entity_snapshot(snapshot_message);
    std::vector<u8> packet = protocol::serialize_envelope(
        {protocol::CurrentProtocolVersion, protocol::MessageType::EntitySnapshot, 0, 0, 0, 0},
        payload);
    (void)transport_->send(
        {session->connection_id, protocol::ReliabilityClass::UnreliableLatestWins, std::move(packet)});
  }
}

void ServerRuntime::send_inventory_updates() {
  for (const SessionID session_id : sessions_.session_ids()) {
    const ServerSession* session = sessions_.find(session_id);
    if (session == nullptr) {
      continue;
    }
    const player::PlayerState* player_state = default_world_.find_player(session->player_id);
    if (player_state == nullptr) {
      continue;
    }

    InventoryReplicationState& state = inventory_replication_[session->player_id];
    const bool changed =
        !state.initialized ||
        state.slots != player_state->inventory.slots() ||
        state.carried_stack != player_state->inventory_cursor_stack ||
        state.selected_hotbar_slot != player_state->selected_hotbar_slot;
    if (!changed) {
      continue;
    }

    ++state.revision;
    state.initialized = true;
    state.slots = player_state->inventory.slots();
    state.carried_stack = player_state->inventory_cursor_stack;
    state.selected_hotbar_slot = player_state->selected_hotbar_slot;

    const protocol::InventoryUpdate update = make_inventory_update(*player_state, state.revision);
    const std::vector<u8> payload = protocol::serialize_inventory_update(update);
    std::vector<u8> packet = protocol::serialize_envelope(
        {protocol::CurrentProtocolVersion, protocol::MessageType::InventoryUpdate, 0, 0, 0, 0},
        payload);
    (void)transport_->send(
        {session->connection_id, protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
  }
}

protocol::InventoryUpdate ServerRuntime::make_inventory_update(
    const player::PlayerState& player_state,
    u32 revision) const {
  protocol::InventoryUpdate update;
  update.inventory_id = protocol::PlayerInventoryID;
  update.revision = revision;
  update.selected_hotbar_slot = player_state.selected_hotbar_slot;
  update.slots = player_state.inventory.slots();
  update.carried_stack = player_state.inventory_cursor_stack;
  return update;
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
        std::pair{std::string_view{"arvane:wood_pickaxe"}, u16{1}},
        std::pair{std::string_view{"arvane:bucket"}, u16{1}},
        std::pair{std::string_view{"arvane:water_bucket"}, u16{1}}}) {
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
