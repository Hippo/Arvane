#pragma once

#include "arv/client/camera_shell.hpp"
#include "arv/client/chunk_interest.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/core/time.hpp"
#include "arv/net/transport.hpp"
#include "arv/protocol/chunk_streaming.hpp"
#include "arv/protocol/handshake.hpp"
#include "arv/protocol/message_envelope.hpp"

#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace arv::client {

class ClientRuntime {
public:
  explicit ClientRuntime(std::unique_ptr<net::ITransport> transport);
  net::ConnectResult connect_integrated();
  void submit_input(InputFrame input);
  void set_chunk_view_distance(i32 view_distance) noexcept;
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
  [[nodiscard]] const std::string& chat_draft() const noexcept {
    return chat_draft_;
  }
  [[nodiscard]] std::span<const std::string> chat_messages() const noexcept {
    return chat_messages_;
  }
  [[nodiscard]] bool tab_menu_visible() const noexcept {
    return tab_menu_visible_;
  }
  [[nodiscard]] std::vector<protocol::ChunkData> drain_received_chunk_data();
  [[nodiscard]] std::vector<protocol::ChunkUnload> drain_received_chunk_unloads();

private:
  net::NetworkResult send_client_hello();
  net::NetworkResult send_chunk_interest_update();
  void handle_network_event(net::NetworkEvent event);
  void handle_message(const std::vector<u8>& packet);
  net::NetworkResult send_input_batch(TickIndex tick_index);
  void handle_chat_input(const InputFrame& input, bool was_chat_open);
  void close_ui() noexcept;

  std::unique_ptr<net::ITransport> transport_;
  net::ConnectionID connection_id_{};
  net::ConnectionState connection_state_{net::ConnectionState::Disconnected};
  LocalPlayerController player_controller_{};
  ClientChunkInterest chunk_interest_{};
  std::vector<net::NetworkEvent> inbound_events_;
  std::vector<protocol::ChunkData> received_chunk_data_;
  std::vector<protocol::ChunkUnload> received_chunk_unloads_;
  bool inventory_open_{};
  bool chat_open_{};
  bool tab_menu_visible_{};
  std::string chat_draft_{};
  std::vector<std::string> chat_messages_{};
};

} // namespace arv::client
