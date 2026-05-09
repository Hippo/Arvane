#pragma once

#include "arv/net/transport.hpp"
#include "arv/player/inventory.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/protocol/inventory_sync.hpp"

#include <optional>

namespace arv::client {

namespace detail {
class ClientProtocolSender;
}

class ClientInventoryRuntime {
public:
  [[nodiscard]] const player::Inventory& inventory() const noexcept;
  [[nodiscard]] const std::optional<player::ItemStack>& cursor_stack() const noexcept;
  [[nodiscard]] u32 revision() const noexcept;
  [[nodiscard]] bool has_authoritative_inventory() const noexcept;

  net::NetworkResult request_slot_click(
      detail::ClientProtocolSender& sender,
      usize slot_index,
      player::InventoryClickButton button,
      net::ConnectionState connection_state);

  net::NetworkResult request_drop_cursor(
      detail::ClientProtocolSender& sender,
      bool one_item,
      net::ConnectionState connection_state);

  net::NetworkResult request_close(
      detail::ClientProtocolSender& sender,
      net::ConnectionState connection_state);

  void apply_update(protocol::ClientboundInventoryUpdate update);
  void reset();

private:
  net::NetworkResult send_request_if_in_game(
      detail::ClientProtocolSender& sender,
      protocol::ServerboundInventoryActionRequest request,
      net::ConnectionState connection_state);

  player::Inventory inventory_{player::DefaultPlayerInventorySlotCount};
  std::optional<player::ItemStack> cursor_stack_{};
  u32 revision_{};
  u32 next_action_id_{1};
  bool has_authoritative_inventory_{};
};

} // namespace arv::client
