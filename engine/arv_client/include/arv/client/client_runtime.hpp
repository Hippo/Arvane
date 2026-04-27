#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace arv::client {

class ClientRuntime {
public:
  explicit ClientRuntime(std::unique_ptr<net::ITransport> transport);
  net::ConnectResult connect_integrated();
  void submit_input(InputFrame input);
  void fixed_tick(TickIndex tick_index);
  [[nodiscard]] render::FramePacket build_frame_packet(
      f32 interpolation_alpha,
      u32 viewport_width = 0,
      u32 viewport_height = 0) const;
  [[nodiscard]] net::ConnectionState connection_state() const noexcept;
  [[nodiscard]] const CameraShell& camera() const noexcept;
  CameraShell& camera() noexcept;
  [[nodiscard]] u8 selected_hotbar_slot() const noexcept;
  [[nodiscard]] const std::string& game_mode_id() const noexcept;
  [[nodiscard]] const player::ResolvedPermissions& resolved_permissions() const noexcept;
  [[nodiscard]] bool inventory_open() const noexcept {
    return inventory_open_;
  }
  [[nodiscard]] bool chat_open() const noexcept {
    return chat_open_;
  }
  [[nodiscard]] bool tab_menu_visible() const noexcept {
    return tab_menu_visible_;
  }

private:
  net::NetworkResult send_client_hello();
  void handle_network_event(net::NetworkEvent event);
  void handle_message(const std::vector<u8>& packet);
  net::NetworkResult send_input_batch(TickIndex tick_index);

  std::unique_ptr<net::ITransport> transport_;
  net::ConnectionID connection_id_{};
  net::ConnectionState connection_state_{net::ConnectionState::Disconnected};
  LocalPlayerController player_controller_{};
  std::vector<net::NetworkEvent> inbound_events_;
  bool inventory_open_{};
  bool chat_open_{};
  bool tab_menu_visible_{};
};

} // namespace arv::client
