#include "arv/client/client_runtime.hpp"

#include <utility>

namespace arv::client {

ClientRuntime::ClientRuntime(std::unique_ptr<net::ITransport> transport) : transport_(std::move(transport)) {
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
  if (input.open_inventory_pressed && player_controller_.resolved_permissions().core.can_open_inventory) {
    inventory_open_ = !inventory_open_;
    if (inventory_open_) {
      chat_open_ = false;
    }
  }
  if (input.open_chat_pressed) {
    chat_open_ = true;
    inventory_open_ = false;
  }

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

void ClientRuntime::fixed_tick(TickIndex tick_index) {
  (void)tick_index;
  if (!transport_) {
    return;
  }

  if (connection_state_ == net::ConnectionState::InGame) {
    (void)send_input_batch(tick_index);
  }

  inbound_events_ = transport_->poll_events();
  for (net::NetworkEvent& event : inbound_events_) {
    handle_network_event(std::move(event));
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

net::NetworkResult ClientRuntime::send_client_hello() {
  protocol::ClientHello hello;
  std::vector<u8> payload = protocol::serialize_client_hello(hello);
  std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, protocol::MessageType::ClientHello, 0, 0, 0, 0},
      payload);
  return transport_->send({connection_id_, protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
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
      connection_state_ = server_hello.value().required_followup_sync
                              ? net::ConnectionState::RegistrySync
                              : net::ConnectionState::InGame;
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
    default:
      return;
  }
}

net::NetworkResult ClientRuntime::send_input_batch(TickIndex tick_index) {
  const player::PlayerCommand command = player_controller_.consume_command(tick_index);
  protocol::ClientInputBatch batch;
  batch.first_input_tick = tick_index;
  batch.inputs.push_back(protocol::input_sample_from_command(command));

  const std::vector<u8> payload = protocol::serialize_client_input_batch(batch);
  const std::vector<u8> packet = protocol::serialize_envelope(
      {protocol::CurrentProtocolVersion, protocol::MessageType::ClientInputBatch, 0, 0, 0, 0},
      payload);
  return transport_->send(
      {connection_id_, protocol::ReliabilityClass::UnreliableLatestWins, std::move(packet)});
}

} // namespace arv::client
