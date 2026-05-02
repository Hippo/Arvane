#include "client_inventory_runtime.hpp"

#include "client_protocol_send.hpp"

#include <utility>

namespace arv::client {

const player::Inventory& ClientInventoryRuntime::inventory() const noexcept {
  return inventory_;
}

const std::optional<player::ItemStack>& ClientInventoryRuntime::cursor_stack() const noexcept {
  return cursor_stack_;
}

u32 ClientInventoryRuntime::revision() const noexcept {
  return revision_;
}

bool ClientInventoryRuntime::has_authoritative_inventory() const noexcept {
  return has_authoritative_inventory_;
}

net::NetworkResult ClientInventoryRuntime::request_slot_click(
    detail::ClientProtocolSender& sender,
    usize slot_index,
    player::InventoryClickButton button,
    net::ConnectionState connection_state) {
  protocol::InventoryActionRequest request;
  request.inventory_id = protocol::PlayerInventoryID;
  request.client_action_id = next_action_id_++;
  request.kind = protocol::InventoryActionKind::SlotClick;
  request.slot_index = static_cast<u32>(slot_index);
  request.button = button;
  return send_request_if_in_game(sender, request, connection_state);
}

net::NetworkResult ClientInventoryRuntime::request_drop_cursor(
    detail::ClientProtocolSender& sender,
    bool one_item,
    net::ConnectionState connection_state) {
  protocol::InventoryActionRequest request;
  request.inventory_id = protocol::PlayerInventoryID;
  request.client_action_id = next_action_id_++;
  request.kind = one_item ? protocol::InventoryActionKind::DropOneCursor
                          : protocol::InventoryActionKind::DropCursor;
  return send_request_if_in_game(sender, request, connection_state);
}

net::NetworkResult ClientInventoryRuntime::request_close(
    detail::ClientProtocolSender& sender,
    net::ConnectionState connection_state) {
  protocol::InventoryActionRequest request;
  request.inventory_id = protocol::PlayerInventoryID;
  request.client_action_id = next_action_id_++;
  request.kind = protocol::InventoryActionKind::CloseMenu;
  return send_request_if_in_game(sender, request, connection_state);
}

void ClientInventoryRuntime::apply_update(protocol::InventoryUpdate update) {
  if (update.inventory_id != protocol::PlayerInventoryID ||
      update.revision <= revision_) {
    return;
  }

  inventory_ = player::Inventory{update.slots.size()};
  for (usize index = 0; index < update.slots.size(); ++index) {
    (void)inventory_.set_slot(index, update.slots[index]);
  }
  cursor_stack_ = update.carried_stack;
  revision_ = update.revision;
  has_authoritative_inventory_ = true;
}

void ClientInventoryRuntime::reset() {
  inventory_ = player::Inventory{player::DefaultPlayerInventorySlotCount};
  cursor_stack_.reset();
  revision_ = 0;
  has_authoritative_inventory_ = false;
}

net::NetworkResult ClientInventoryRuntime::send_request_if_in_game(
    detail::ClientProtocolSender& sender,
    protocol::InventoryActionRequest request,
    net::ConnectionState connection_state) {
  if (connection_state != net::ConnectionState::InGame) {
    return net::NetworkResult::failure(net::NetworkError{"client is not in game"});
  }
  return sender.send_inventory_action(request);
}

} // namespace arv::client
