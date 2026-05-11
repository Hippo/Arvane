#pragma once

#include "arv/client/local_player_controller.hpp"
#include "arv/core/time.hpp"
#include "arv/net/connection_state.hpp"

namespace arv::client {

class ClientUiState;

namespace detail {

class ClientProtocolSender;

class ClientInputRuntime {
public:
  void submit_input(
      InputFrame input,
      ClientUiState& ui,
      LocalPlayerController& player_controller,
      ClientProtocolSender& sender,
      net::ConnectionState connection_state);

  void send_fixed_tick_input(
      TickIndex tick_index,
      LocalPlayerController& player_controller,
      ClientProtocolSender& sender,
      const physics::ICollisionQuery* collision_query,
      Seconds tick_seconds);

  void set_movement_send_rate(f32 send_rate_hz) noexcept;

private:
  f32 movement_send_rate_hz_{protocol::DefaultClientMovementSendRateHz};
  Seconds movement_send_accumulator_seconds_{};
  u32 next_movement_sequence_{1};
  u8 last_sent_hotbar_slot_{};
  bool sent_movement_packet_{};
};

} // namespace detail
} // namespace arv::client
