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
  explicit ClientRuntime(std::unique_ptr<net::ITransport> transport);
  net::ConnectResult connect_integrated();
  void submit_input(InputFrame input);
  void update_freecam(Seconds delta_seconds) noexcept;
  void fixed_tick(TickIndex tick_index);
  [[nodiscard]] render::FramePacket build_frame_packet(
      f32 interpolation_alpha,
      u32 viewport_width = 0,
      u32 viewport_height = 0) const;
  [[nodiscard]] net::ConnectionState connection_state() const noexcept;
  [[nodiscard]] const CameraShell& camera() const noexcept;
  CameraShell& camera() noexcept;

private:
  net::NetworkResult send_client_hello();
  void handle_network_event(net::NetworkEvent event);
  void handle_message(const std::vector<u8>& packet);

  std::unique_ptr<net::ITransport> transport_;
  net::ConnectionID connection_id_{};
  net::ConnectionState connection_state_{net::ConnectionState::Disconnected};
  CameraShell camera_{};
  InputFrame latest_input_{};
  std::vector<net::NetworkEvent> inbound_events_;
};

} // namespace arv::client
