#pragma once

#include "arv/client/local_player_controller.hpp"
#include "arv/core/time.hpp"

namespace arv::client {

class ClientUiState;

namespace detail {

class ClientProtocolSender;

class ClientInputRuntime {
public:
  void submit_input(
      InputFrame input,
      ClientUiState& ui,
      LocalPlayerController& player_controller);

  void send_fixed_tick_input(
      TickIndex tick_index,
      LocalPlayerController& player_controller,
      ClientProtocolSender& sender);
};

} // namespace detail
} // namespace arv::client
