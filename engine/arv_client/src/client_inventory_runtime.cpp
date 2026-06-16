#include "client_inventory_runtime.hpp"

#include "client_protocol_send.hpp"

#include <algorithm>
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

bool ClientInventoryRuntime::container_open() const noexcept {
  return active_container_.has_value();
}

u32 ClientInventoryRuntime::active_container_session_id() const noexcept {
  return active_container_.has_value() ? active_container_->session_id : 0U;
}

const std::string& ClientInventoryRuntime::active_container_type() const noexcept {
  static const std::string Empty;
  return active_container_.has_value() ? active_container_->type : Empty;
}

const std::string& ClientInventoryRuntime::active_container_title() const noexcept {
  static const std::string Empty;
  return active_container_.has_value() ? active_container_->title : Empty;
}

const std::string& ClientInventoryRuntime::active_container_layout() const noexcept {
  static const std::string Empty;
  return active_container_.has_value() ? active_container_->layout : Empty;
}

const player::Inventory& ClientInventoryRuntime::active_container_inventory() const noexcept {
  static const player::Inventory EmptyInventory;
  return active_container_.has_value() ? active_container_->inventory : EmptyInventory;
}

std::optional<protocol::ContainerBlockSource>
ClientInventoryRuntime::active_container_block_source() const noexcept {
  return active_container_.has_value() ? active_container_->block_source : std::nullopt;
}

net::NetworkResult ClientInventoryRuntime::request_open_container_block(
    detail::ClientProtocolSender& sender,
    WorldID world_id,
    BlockPos block,
    net::ConnectionState connection_state) {
  if (connection_state != net::ConnectionState::InGame) {
    return net::NetworkResult::failure(net::NetworkError{"client is not in game"});
  }
  return sender.send_open_container_block({world_id, block, next_action_id_++});
}

net::NetworkResult ClientInventoryRuntime::request_container_slot_click(
    detail::ClientProtocolSender& sender,
    player::ContainerSlotSource source,
    usize slot_index,
    player::InventoryClickButton button,
    net::ConnectionState connection_state) {
  if (!active_container_.has_value()) {
    return net::NetworkResult::failure(net::NetworkError{"no open container"});
  }
  protocol::ServerboundContainerAction request;
  request.session_id = active_container_->session_id;
  request.client_action_id = next_action_id_++;
  request.kind = protocol::ContainerActionKind::SlotClick;
  request.source = source;
  request.slot_index = static_cast<u32>(slot_index);
  request.button = button;
  return send_container_action_if_in_game(sender, request, connection_state);
}

net::NetworkResult ClientInventoryRuntime::request_container_shift_transfer(
    detail::ClientProtocolSender& sender,
    player::ContainerSlotSource source,
    usize slot_index,
    net::ConnectionState connection_state) {
  if (!active_container_.has_value()) {
    return net::NetworkResult::failure(net::NetworkError{"no open container"});
  }
  protocol::ServerboundContainerAction request;
  request.session_id = active_container_->session_id;
  request.client_action_id = next_action_id_++;
  request.kind = protocol::ContainerActionKind::ShiftTransfer;
  request.source = source;
  request.slot_index = static_cast<u32>(slot_index);
  return send_container_action_if_in_game(sender, request, connection_state);
}

net::NetworkResult ClientInventoryRuntime::request_slot_click(
    detail::ClientProtocolSender& sender,
    usize slot_index,
    player::InventoryClickButton button,
    net::ConnectionState connection_state) {
  protocol::ServerboundInventoryActionRequest request;
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
  if (active_container_.has_value()) {
    protocol::ServerboundContainerAction request;
    request.session_id = active_container_->session_id;
    request.client_action_id = next_action_id_++;
    request.kind = one_item ? protocol::ContainerActionKind::DropOneCursor
                            : protocol::ContainerActionKind::DropCursor;
    return send_container_action_if_in_game(sender, request, connection_state);
  }

  protocol::ServerboundInventoryActionRequest request;
  request.inventory_id = protocol::PlayerInventoryID;
  request.client_action_id = next_action_id_++;
  request.kind = one_item ? protocol::InventoryActionKind::DropOneCursor
                          : protocol::InventoryActionKind::DropCursor;
  return send_request_if_in_game(sender, request, connection_state);
}

net::NetworkResult ClientInventoryRuntime::request_close(
    detail::ClientProtocolSender& sender,
    net::ConnectionState connection_state) {
  if (active_container_.has_value()) {
    if (connection_state != net::ConnectionState::InGame) {
      return net::NetworkResult::failure(net::NetworkError{"client is not in game"});
    }
    const protocol::ServerboundCloseContainer request{
        active_container_->session_id,
        next_action_id_++};
    active_container_.reset();
    return sender.send_close_container(request);
  }

  protocol::ServerboundInventoryActionRequest request;
  request.inventory_id = protocol::PlayerInventoryID;
  request.client_action_id = next_action_id_++;
  request.kind = protocol::InventoryActionKind::CloseMenu;
  return send_request_if_in_game(sender, request, connection_state);
}

void ClientInventoryRuntime::apply_update(protocol::ClientboundInventoryUpdate update) {
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

void ClientInventoryRuntime::apply_container_open(protocol::ClientboundOpenContainer open) {
  ActiveContainer container;
  container.session_id = open.session_id;
  container.revision = open.revision;
  container.type = std::move(open.container_type);
  container.title = std::move(open.title);
  container.layout = std::move(open.ui_layout);
  container.inventory = player::Inventory{open.container_slots.size()};
  for (usize index = 0; index < open.container_slots.size(); ++index) {
    (void)container.inventory.set_slot(index, open.container_slots[index]);
  }
  container.block_source = std::move(open.block_source);
  active_container_ = std::move(container);

  inventory_ = player::Inventory{open.player_slots.size()};
  for (usize index = 0; index < open.player_slots.size(); ++index) {
    (void)inventory_.set_slot(index, open.player_slots[index]);
  }
  cursor_stack_ = std::move(open.carried_stack);
  has_authoritative_inventory_ = true;
}

void ClientInventoryRuntime::apply_container_content(
    protocol::ClientboundContainerSetContent content) {
  if (!active_container_.has_value() ||
      content.session_id != active_container_->session_id ||
      content.revision <= active_container_->revision) {
    return;
  }

  active_container_->inventory = player::Inventory{content.container_slots.size()};
  for (usize index = 0; index < content.container_slots.size(); ++index) {
    (void)active_container_->inventory.set_slot(index, content.container_slots[index]);
  }
  active_container_->revision = content.revision;

  inventory_ = player::Inventory{content.player_slots.size()};
  for (usize index = 0; index < content.player_slots.size(); ++index) {
    (void)inventory_.set_slot(index, content.player_slots[index]);
  }
  cursor_stack_ = std::move(content.carried_stack);
  has_authoritative_inventory_ = true;
}

void ClientInventoryRuntime::apply_container_slot(protocol::ClientboundContainerSetSlot slot) {
  if (!active_container_.has_value() ||
      slot.session_id != active_container_->session_id ||
      slot.revision <= active_container_->revision) {
    return;
  }

  player::Inventory* target = nullptr;
  switch (slot.source) {
    case player::ContainerSlotSource::OpenedContainer:
      target = &active_container_->inventory;
      break;
    case player::ContainerSlotSource::PlayerInventory:
      target = &inventory_;
      break;
  }
  if (target != nullptr && target->is_valid_slot(slot.slot_index)) {
    (void)target->set_slot(slot.slot_index, slot.stack);
  }
  active_container_->revision = slot.revision;
  cursor_stack_ = std::move(slot.carried_stack);
}

void ClientInventoryRuntime::apply_container_close(protocol::ClientboundCloseContainer close) {
  if (active_container_.has_value() &&
      close.session_id == active_container_->session_id) {
    active_container_.reset();
  }
}

void ClientInventoryRuntime::reset() {
  inventory_ = player::Inventory{player::DefaultPlayerInventorySlotCount};
  cursor_stack_.reset();
  active_container_.reset();
  revision_ = 0;
  has_authoritative_inventory_ = false;
}

net::NetworkResult ClientInventoryRuntime::send_request_if_in_game(
    detail::ClientProtocolSender& sender,
    protocol::ServerboundInventoryActionRequest request,
    net::ConnectionState connection_state) {
  if (connection_state != net::ConnectionState::InGame) {
    return net::NetworkResult::failure(net::NetworkError{"client is not in game"});
  }
  return sender.send_inventory_action(request);
}

net::NetworkResult ClientInventoryRuntime::send_container_action_if_in_game(
    detail::ClientProtocolSender& sender,
    protocol::ServerboundContainerAction request,
    net::ConnectionState connection_state) {
  if (connection_state != net::ConnectionState::InGame) {
    return net::NetworkResult::failure(net::NetworkError{"client is not in game"});
  }
  return sender.send_container_action(request);
}

} // namespace arv::client
