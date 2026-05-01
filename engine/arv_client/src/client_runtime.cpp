#include "arv/client/client_runtime.hpp"

#include <utility>

namespace arv::client {
namespace {

constexpr usize MaxChatDraftBytes = 256U;
constexpr usize MaxChatMessages = 64U;

void erase_last_utf8_codepoint(std::string& text) {
  if (text.empty()) {
    return;
  }
  usize index = text.size() - 1U;
  while (index > 0U &&
         (static_cast<unsigned char>(text[index]) & 0xC0U) == 0x80U) {
    --index;
  }
  text.erase(index);
}

} // namespace

ClientRuntime::ClientRuntime(std::unique_ptr<net::ITransport> transport) : transport_(std::move(transport)) {
}

void ClientRuntime::set_player_tuning(player::PlayerTuning tuning) noexcept {
  player_controller_.set_player_tuning(tuning);
}

net::ConnectResult ClientRuntime::connect_integrated() {
  if (!transport_) {
    return net::ConnectResult::failure(net::NetworkError{"client has no transport"});
  }
  auto result = transport_->connect({"in-process", 0});
  if (result) {
    connection_id_ = result.value();
    connection_state_ = net::ConnectionState::TransportConnected;
    auto hello_result = send_client_hello();
    if (!hello_result) {
      return net::ConnectResult::failure(hello_result.error());
    }
    connection_state_ = net::ConnectionState::ProtocolHandshake;
  }
  return result;
}

void ClientRuntime::submit_input(InputFrame input) {
  tab_menu_visible_ = input.tab_menu_held;
  const bool was_chat_open = chat_open_;

  if (input.text_cancel_pressed && (inventory_open_ || chat_open_)) {
    close_ui();
  } else if (!chat_open_ && input.open_inventory_pressed &&
             player_controller_.resolved_permissions().core.can_open_inventory) {
    inventory_open_ = !inventory_open_;
    if (inventory_open_) {
      chat_open_ = false;
    }
  }
  if (!was_chat_open && input.open_chat_pressed) {
    chat_open_ = true;
    inventory_open_ = false;
  }

  handle_chat_input(input, was_chat_open);

  if (inventory_open_ || chat_open_) {
    input.move_forward = false;
    input.move_backward = false;
    input.move_left = false;
    input.move_right = false;
    input.jump_pressed = false;
    input.jump_held = false;
    input.sneak_held = false;
    input.sprint_held = false;
    input.primary_pressed = false;
    input.primary_held = false;
    input.secondary_pressed = false;
    input.secondary_held = false;
    input.toggle_fly_pressed = false;
    input.hotbar_select.reset();
    input.hotbar_scroll_steps = 0;
    input.mouse_delta_x = 0.0F;
    input.mouse_delta_y = 0.0F;
  }

  player_controller_.submit_input(std::move(input));
}

void ClientRuntime::set_chunk_view_distance(i32 view_distance) noexcept {
  chunk_interest_.set_view_distance(view_distance);
}

void ClientRuntime::fixed_tick(TickIndex tick_index) {
  if (!transport_) {
    return;
  }

  const bool was_in_game = connection_state_ == net::ConnectionState::InGame;

  inbound_events_ = transport_->poll_events();
  for (net::NetworkEvent& event : inbound_events_) {
    handle_network_event(std::move(event));
  }

  if (was_in_game && connection_state_ == net::ConnectionState::InGame) {
    (void)send_input_batch(tick_index);
  }

  if (connection_state_ == net::ConnectionState::InGame) {
    chunk_interest_.set_world_id(player_controller_.camera().world_id());
    chunk_interest_.update_player_position(player_controller_.player_position());
    if (chunk_interest_.consume_interest_changed()) {
      (void)send_chunk_interest_update();
    }
  }
}

render::FramePacket ClientRuntime::build_frame_packet(f32 interpolation_alpha, u32 viewport_width, u32 viewport_height) const {
  render::FramePacket packet;
  packet.camera = player_controller_.camera().snapshot(viewport_width, viewport_height, interpolation_alpha);
  packet.world_view = render::render_view_from_camera(packet.camera);
  return packet;
}

net::ConnectionState ClientRuntime::connection_state() const noexcept {
  return connection_state_;
}

const CameraShell& ClientRuntime::camera() const noexcept {
  return player_controller_.camera();
}

CameraShell& ClientRuntime::camera() noexcept {
  return player_controller_.camera();
}

u8 ClientRuntime::selected_hotbar_slot() const noexcept {
  return player_controller_.selected_hotbar_slot();
}

const std::string& ClientRuntime::game_mode_id() const noexcept {
  return player_controller_.game_mode_id();
}

const player::ResolvedPermissions& ClientRuntime::resolved_permissions() const noexcept {
  return player_controller_.resolved_permissions();
}

std::vector<protocol::ChunkData> ClientRuntime::drain_received_chunk_data() {
  std::vector<protocol::ChunkData> drained = std::move(received_chunk_data_);
  received_chunk_data_.clear();
  return drained;
}

std::vector<protocol::ChunkUnload> ClientRuntime::drain_received_chunk_unloads() {
  std::vector<protocol::ChunkUnload> drained = std::move(received_chunk_unloads_);
  received_chunk_unloads_.clear();
  return drained;
}

std::vector<protocol::BlockUpdateBatch> ClientRuntime::drain_received_block_updates() {
  std::vector<protocol::BlockUpdateBatch> drained = std::move(received_block_updates_);
  received_block_updates_.clear();
  return drained;
}

net::NetworkResult ClientRuntime::request_inventory_slot_click(
    usize slot_index,
    player::InventoryClickButton button) {
  protocol::InventoryActionRequest request;
  request.inventory_id = protocol::PlayerInventoryID;
  request.client_action_id = next_inventory_action_id_++;
  request.kind = protocol::InventoryActionKind::SlotClick;
  request.slot_index = static_cast<u32>(slot_index);
  request.button = button;
  return send_inventory_action(request);
}

net::NetworkResult ClientRuntime::request_inventory_drop_cursor(bool one_item) {
  protocol::InventoryActionRequest request;
  request.inventory_id = protocol::PlayerInventoryID;
  request.client_action_id = next_inventory_action_id_++;
  request.kind = one_item ? protocol::InventoryActionKind::DropOneCursor
                          : protocol::InventoryActionKind::DropCursor;
  return send_inventory_action(request);
}

net::NetworkResult ClientRuntime::request_inventory_close() {
  protocol::InventoryActionRequest request;
  request.inventory_id = protocol::PlayerInventoryID;
  request.client_action_id = next_inventory_action_id_++;
  request.kind = protocol::InventoryActionKind::CloseMenu;
  return send_inventory_action(request);
}

const ReplicatedEntityMirror* ClientRuntime::find_replicated_entity(
    EntityNetID entity_id) const noexcept {
  const auto found = replicated_entities_.find(entity_id);
  return found == replicated_entities_.end() ? nullptr : &found->second;
}

std::vector<ReplicatedEntityMirror> ClientRuntime::replicated_entity_mirrors() const {
  std::vector<ReplicatedEntityMirror> mirrors;
  mirrors.reserve(replicated_entities_.size());
  for (const auto& [entity_id, mirror] : replicated_entities_) {
    (void)entity_id;
    mirrors.push_back(mirror);
  }
  return mirrors;
}

void ClientRuntime::handle_chat_input(const InputFrame& input, bool was_chat_open) {
  if (!chat_open_) {
    return;
  }

  if (was_chat_open && input.text_backspace_pressed) {
    erase_last_utf8_codepoint(chat_draft_);
  }
  if (was_chat_open && !input.text_input.empty()) {
    const usize remaining =
        chat_draft_.size() >= MaxChatDraftBytes ? 0U : MaxChatDraftBytes - chat_draft_.size();
    chat_draft_.append(input.text_input.substr(0U, remaining));
  }
  if (was_chat_open && input.text_submit_pressed) {
    if (!chat_draft_.empty()) {
      chat_messages_.emplace_back(chat_draft_);
      if (chat_messages_.size() > MaxChatMessages) {
        chat_messages_.erase(chat_messages_.begin());
      }
      chat_draft_.clear();
    }
    chat_open_ = false;
  }
}

void ClientRuntime::close_ui() noexcept {
  inventory_open_ = false;
  chat_open_ = false;
  chat_draft_.clear();
}

net::NetworkResult ClientRuntime::send_client_hello() {
  protocol::ClientHello hello;
  std::vector<u8> payload = protocol::serialize_client_hello(hello);
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, protocol::MessageType::ClientHello, 0, 0, 0, 0},
      payload);
  return transport_->send({connection_id_, protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
}

net::NetworkResult ClientRuntime::send_chunk_interest_update() {
  const protocol::ChunkInterestUpdate update = chunk_interest_.build_interest_update();
  const std::vector<u8> payload = protocol::serialize_chunk_interest_update(update);
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, protocol::MessageType::ChunkInterestUpdate, 0, 0, 0, 0},
      payload);
  return transport_->send(
      {connection_id_, protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
}

net::NetworkResult ClientRuntime::send_inventory_action(protocol::InventoryActionRequest request) {
  if (!transport_ || connection_state_ != net::ConnectionState::InGame) {
    return net::NetworkResult::failure(net::NetworkError{"client is not in game"});
  }
  const std::vector<u8> payload = protocol::serialize_inventory_action_request(request);
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, protocol::MessageType::InventoryActionRequest, 0, 0, 0, 0},
      payload);
  return transport_->send(
      {connection_id_, protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
}

void ClientRuntime::handle_network_event(net::NetworkEvent event) {
  switch (event.type) {
    case net::NetworkEventType::ConnectionRequested:
      return;
    case net::NetworkEventType::ConnectionAccepted:
      if (connection_state_ == net::ConnectionState::Connecting) {
        connection_state_ = net::ConnectionState::TransportConnected;
      }
      return;
    case net::NetworkEventType::ConnectionClosed:
    case net::NetworkEventType::ConnectionFailed:
      connection_state_ = net::ConnectionState::Disconnected;
      return;
    case net::NetworkEventType::MessageReceived:
      handle_message(event.payload);
      return;
    case net::NetworkEventType::MessageSendFailed:
      connection_state_ = net::ConnectionState::Disconnecting;
      return;
  }
}

void ClientRuntime::handle_message(const std::vector<u8>& packet) {
  auto decoded = protocol::deserialize_envelope(packet);
  if (!decoded) {
    return;
  }

  switch (decoded.value().envelope.message_type) {
    case protocol::MessageType::ServerHello: {
      auto server_hello = protocol::deserialize_server_hello(decoded.value().payload);
      if (!server_hello) {
        connection_state_ = net::ConnectionState::Disconnecting;
        return;
      }

      if (!server_hello.value().accepted) {
        connection_state_ = net::ConnectionState::Disconnecting;
        return;
      }

      player_controller_.camera().set_world_id(server_hello.value().initial_world_id);
      chunk_interest_.set_world_id(server_hello.value().initial_world_id);
      connection_state_ = server_hello.value().required_followup_sync
                              ? net::ConnectionState::RegistrySync
                              : net::ConnectionState::WorldJoin;
      return;
    }
    case protocol::MessageType::WorldJoinResult: {
      auto result = protocol::deserialize_world_join_result(decoded.value().payload);
      if (!result) {
        connection_state_ = net::ConnectionState::Disconnecting;
        return;
      }
      apply_world_join_result(result.value());
      return;
    }
    case protocol::MessageType::WorldLeave: {
      auto leave = protocol::deserialize_world_leave(decoded.value().payload);
      if (!leave) {
        return;
      }
      apply_world_leave(leave.value());
      return;
    }
    case protocol::MessageType::PlayerCorrection: {
      auto correction = protocol::deserialize_player_correction(decoded.value().payload);
      if (!correction) {
        return;
      }
      player_controller_.apply_authoritative_state(correction.value());
      return;
    }
    case protocol::MessageType::ChunkData: {
      auto chunk_data = protocol::deserialize_chunk_data(decoded.value().payload);
      if (!chunk_data) {
        return;
      }
      received_chunk_data_.emplace_back(std::move(chunk_data.value()));
      return;
    }
    case protocol::MessageType::ChunkUnload: {
      auto unload = protocol::deserialize_chunk_unload(decoded.value().payload);
      if (!unload) {
        return;
      }
      received_chunk_unloads_.emplace_back(unload.value());
      return;
    }
    case protocol::MessageType::BlockUpdateBatch: {
      auto updates = protocol::deserialize_block_update_batch(decoded.value().payload);
      if (!updates) {
        return;
      }
      if (updates.value().world_id == player_controller_.camera().world_id()) {
        received_block_updates_.emplace_back(std::move(updates.value()));
      }
      return;
    }
    case protocol::MessageType::EntitySpawn: {
      auto spawn = protocol::deserialize_entity_spawn(decoded.value().payload);
      if (!spawn) {
        return;
      }
      apply_entity_spawn(std::move(spawn.value()));
      return;
    }
    case protocol::MessageType::EntityDespawn: {
      auto despawn = protocol::deserialize_entity_despawn(decoded.value().payload);
      if (!despawn) {
        return;
      }
      apply_entity_despawn(despawn.value());
      return;
    }
    case protocol::MessageType::EntitySnapshot: {
      auto snapshot = protocol::deserialize_entity_snapshot(decoded.value().payload);
      if (!snapshot) {
        return;
      }
      apply_entity_snapshot(snapshot.value());
      return;
    }
    case protocol::MessageType::InventoryUpdate: {
      auto update = protocol::deserialize_inventory_update(decoded.value().payload);
      if (!update) {
        return;
      }
      apply_inventory_update(std::move(update.value()));
      return;
    }
    default:
      return;
  }
}

void ClientRuntime::apply_world_join_result(const protocol::WorldJoinResult& result) {
  if (!result.accepted) {
    connection_state_ = net::ConnectionState::Disconnecting;
    return;
  }
  player_controller_.camera().set_world_id(result.world_id);
  player_controller_.camera().set_position(result.spawn_position);
  chunk_interest_.set_world_id(result.world_id);
  connection_state_ = net::ConnectionState::InGame;
}

void ClientRuntime::apply_world_leave(const protocol::WorldLeave& leave) {
  if (leave.world_id != player_controller_.camera().world_id()) {
    return;
  }
  replicated_entities_.clear();
  received_chunk_data_.clear();
  received_chunk_unloads_.clear();
  received_block_updates_.clear();
  inventory_ = player::Inventory{player::DefaultPlayerInventorySlotCount};
  inventory_cursor_stack_.reset();
  inventory_revision_ = 0;
  has_authoritative_inventory_ = false;
  player_controller_.camera().set_world_id(WorldID{});
  chunk_interest_.set_world_id(WorldID{});
  connection_state_ = net::ConnectionState::WorldJoin;
}

void ClientRuntime::apply_entity_spawn(protocol::EntitySpawn spawn) {
  if (spawn.world_id != player_controller_.camera().world_id()) {
    return;
  }
  ReplicatedEntityMirror& mirror = replicated_entities_[spawn.entity_id];
  mirror.entity_id = spawn.entity_id;
  mirror.entity_type = spawn.entity_type;
  mirror.entity_type_name = std::move(spawn.entity_type_name);
  mirror.world_id = spawn.world_id;
  mirror.previous_position = spawn.position;
  mirror.position = spawn.position;
  mirror.velocity = spawn.velocity;
  mirror.previous_rotation = spawn.rotation;
  mirror.rotation = spawn.rotation;
  mirror.owning_player = spawn.owning_player;
}

void ClientRuntime::apply_entity_despawn(const protocol::EntityDespawn& despawn) {
  if (despawn.world_id != player_controller_.camera().world_id()) {
    return;
  }
  replicated_entities_.erase(despawn.entity_id);
}

void ClientRuntime::apply_entity_snapshot(const protocol::EntitySnapshot& snapshot) {
  if (snapshot.world_id != player_controller_.camera().world_id()) {
    return;
  }
  // TODO(arvane): Add client prediction replay/reconciliation history once
  // predicted entity movement is specified.
  for (const protocol::EntitySnapshotEntry& entry : snapshot.entities) {
    auto found = replicated_entities_.find(entry.entity_id);
    if (found == replicated_entities_.end()) {
      continue;
    }
    ReplicatedEntityMirror& mirror = found->second;
    if (snapshot.server_tick < mirror.last_server_tick) {
      continue;
    }
    mirror.last_server_tick = snapshot.server_tick;
    mirror.previous_position = mirror.position;
    mirror.previous_rotation = mirror.rotation;
    mirror.position = entry.position;
    mirror.velocity = entry.velocity;
    mirror.rotation = entry.rotation;
  }
}

void ClientRuntime::apply_inventory_update(protocol::InventoryUpdate update) {
  if (update.inventory_id != protocol::PlayerInventoryID ||
      update.revision <= inventory_revision_) {
    return;
  }

  inventory_ = player::Inventory{update.slots.size()};
  for (usize index = 0; index < update.slots.size(); ++index) {
    (void)inventory_.set_slot(index, update.slots[index]);
  }
  inventory_cursor_stack_ = update.carried_stack;
  inventory_revision_ = update.revision;
  has_authoritative_inventory_ = true;
}

net::NetworkResult ClientRuntime::send_input_batch(TickIndex tick_index) {
  const player::PlayerCommand command = player_controller_.consume_command(tick_index);
  protocol::ClientInputBatch batch;
  batch.first_input_tick = tick_index;
  batch.inputs.emplace_back(protocol::input_sample_from_command(command));

  const std::vector<u8> payload = protocol::serialize_client_input_batch(batch);
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, protocol::MessageType::ClientInputBatch, 0, 0, 0, 0},
      payload);
  return transport_->send(
      {connection_id_, protocol::ReliabilityClass::UnreliableLatestWins, std::move(packet)});
}

} // namespace arv::client
