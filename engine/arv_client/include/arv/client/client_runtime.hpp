#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace arv::client {

struct InputFrame {
  bool move_forward{};
  bool move_backward{};
  bool move_left{};
  bool move_right{};
  bool jump{};
  bool move_down{};
  bool fast{};
  f32 mouse_delta_x{};
  f32 mouse_delta_y{};
};

class ClientRuntime {
public:
  explicit ClientRuntime(std::unique_ptr<net::ITransport> transport)
      : transport_(std::move(transport)) {}

  net::ConnectResult connect_integrated() {
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

  void submit_input(InputFrame input) {
    latest_input_ = input;
  }

  void update_freecam(Seconds delta_seconds) noexcept {
    camera_.apply_freecam_input({
        latest_input_.move_forward,
        latest_input_.move_backward,
        latest_input_.move_left,
        latest_input_.move_right,
        latest_input_.jump,
        latest_input_.move_down,
        latest_input_.fast,
        latest_input_.mouse_delta_x,
        latest_input_.mouse_delta_y,
        delta_seconds});
  }

  void fixed_tick(TickIndex tick_index) {
    (void)tick_index;
    if (!transport_) {
      return;
    }

    inbound_events_ = transport_->poll_events();
    for (net::NetworkEvent& event : inbound_events_) {
      handle_network_event(std::move(event));
    }
  }

  [[nodiscard]] render::FramePacket build_frame_packet(
      f32 interpolation_alpha,
      u32 viewport_width = 0,
      u32 viewport_height = 0) const {
    render::FramePacket packet;
    packet.camera = camera_.snapshot(viewport_width, viewport_height, interpolation_alpha);
    packet.world_view = render::render_view_from_camera(packet.camera);
    return packet;
  }

  [[nodiscard]] net::ConnectionState connection_state() const noexcept {
    return connection_state_;
  }

  [[nodiscard]] const CameraShell& camera() const noexcept {
    return camera_;
  }

  CameraShell& camera() noexcept {
    return camera_;
  }

private:
  net::NetworkResult send_client_hello() {
    protocol::ClientHello hello;
    std::vector<u8> payload = protocol::serialize_client_hello(hello);
    std::vector<u8> packet = protocol::serialize_envelope(
        {protocol::CurrentProtocolVersion, protocol::MessageType::ClientHello, 0, 0, 0, 0},
        payload);
    return transport_->send({connection_id_, protocol::ReliabilityClass::ReliableOrdered, std::move(packet)});
  }

  void handle_network_event(net::NetworkEvent event) {
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

  void handle_message(const std::vector<u8>& packet) {
    auto decoded = protocol::deserialize_envelope(packet);
    if (!decoded || decoded.value().envelope.message_type != protocol::MessageType::ServerHello) {
      return;
    }

    auto server_hello = protocol::deserialize_server_hello(decoded.value().payload);
    if (!server_hello) {
      connection_state_ = net::ConnectionState::Disconnecting;
      return;
    }

    if (!server_hello.value().accepted) {
      connection_state_ = net::ConnectionState::Disconnecting;
      return;
    }

    camera_.set_world_id(server_hello.value().initial_world_id);
    connection_state_ = server_hello.value().required_followup_sync
                            ? net::ConnectionState::RegistrySync
                            : net::ConnectionState::InGame;
  }

  std::unique_ptr<net::ITransport> transport_;
  net::ConnectionID connection_id_{};
  net::ConnectionState connection_state_{net::ConnectionState::Disconnected};
  CameraShell camera_{};
  InputFrame latest_input_{};
  std::vector<net::NetworkEvent> inbound_events_;
};

} // namespace arv::client
