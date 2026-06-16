#pragma once

#include "arv/net/transport.hpp"
#include "arv/player/inventory.hpp"
#include "arv/player/player_constants.hpp"
#include "arv/protocol/container_sync.hpp"
#include "arv/protocol/inventory_sync.hpp"

#include <optional>
#include <string>

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
  [[nodiscard]] bool container_open() const noexcept;
  [[nodiscard]] u32 active_container_session_id() const noexcept;
  [[nodiscard]] const std::string& active_container_type() const noexcept;
  [[nodiscard]] const std::string& active_container_title() const noexcept;
  [[nodiscard]] const std::string& active_container_layout() const noexcept;
  [[nodiscard]] const player::Inventory& active_container_inventory() const noexcept;

  net::NetworkResult request_open_container_block(
      detail::ClientProtocolSender& sender,
      WorldID world_id,
      BlockPos block,
      net::ConnectionState connection_state);

  net::NetworkResult request_container_slot_click(
      detail::ClientProtocolSender& sender,
      player::ContainerSlotSource source,
      usize slot_index,
      player::InventoryClickButton button,
      net::ConnectionState connection_state);

  net::NetworkResult request_container_shift_transfer(
      detail::ClientProtocolSender& sender,
      player::ContainerSlotSource source,
      usize slot_index,
      net::ConnectionState connection_state);

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
  void apply_container_open(protocol::ClientboundOpenContainer open);
  void apply_container_content(protocol::ClientboundContainerSetContent content);
  void apply_container_slot(protocol::ClientboundContainerSetSlot slot);
  void apply_container_close(protocol::ClientboundCloseContainer close);
  void reset();

private:
  net::NetworkResult send_request_if_in_game(
      detail::ClientProtocolSender& sender,
      protocol::ServerboundInventoryActionRequest request,
      net::ConnectionState connection_state);
  net::NetworkResult send_container_action_if_in_game(
      detail::ClientProtocolSender& sender,
      protocol::ServerboundContainerAction request,
      net::ConnectionState connection_state);

  struct ActiveContainer {
    u32 session_id{};
    u32 revision{};
    std::string type{};
    std::string title{};
    std::string layout{};
    player::Inventory inventory{};
  };

  player::Inventory inventory_{player::DefaultPlayerInventorySlotCount};
  std::optional<player::ItemStack> cursor_stack_{};
  std::optional<ActiveContainer> active_container_{};
  u32 revision_{};
  u32 next_action_id_{1};
  bool has_authoritative_inventory_{};
};

} // namespace arv::client
