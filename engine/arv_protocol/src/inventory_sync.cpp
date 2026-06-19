#include "arv/protocol/inventory_sync.hpp"
#include "arv/protocol/item_stack_wire.hpp"
#include "arv/protocol/message_primitives.hpp"

#include <utility>

namespace arv::protocol {
namespace {

[[nodiscard]] bool valid_action_kind(InventoryActionKind kind) noexcept {
  switch (kind) {
    case InventoryActionKind::SlotClick:
    case InventoryActionKind::DropCursor:
    case InventoryActionKind::DropOneCursor:
    case InventoryActionKind::CloseMenu:
    case InventoryActionKind::ShiftTransfer:
    case InventoryActionKind::DropOneSlot:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_click_button(player::InventoryClickButton button) noexcept {
  switch (button) {
    case player::InventoryClickButton::Primary:
    case player::InventoryClickButton::Secondary:
      return true;
  }
  return false;
}

} // namespace

std::vector<u8> serialize_serverbound_inventory_action_request(const ServerboundInventoryActionRequest& request) {
  MessageWriter writer;
  writer.write<u32>(request.inventory_id.value);
  writer.write<u32>(request.client_action_id);
  writer.write<InventoryActionKind>(request.kind);
  writer.write<u32>(request.slot_index);
  writer.write<player::InventoryClickButton>(request.button);
  return std::move(writer).take_bytes();
}

Expected<ServerboundInventoryActionRequest, ProtocolError> deserialize_serverbound_inventory_action_request(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto inventory_id = reader.read<u32>();
  auto action_id = reader.read<u32>();
  auto kind = reader.read<InventoryActionKind>();
  auto slot = reader.read<u32>();
  auto button = reader.read<player::InventoryClickButton>();
  if (!inventory_id || !action_id || !kind || !slot || !button) {
    return Expected<ServerboundInventoryActionRequest, ProtocolError>::failure(
        ProtocolError{"failed to decode inventory action request"});
  }
  if (!valid_action_kind(kind.value()) || !valid_click_button(button.value())) {
    return Expected<ServerboundInventoryActionRequest, ProtocolError>::failure(
        ProtocolError{"invalid inventory action enum value"});
  }
  auto trailing = require_no_trailing_bytes(reader, "inventory message");
  if (!trailing) {
    return Expected<ServerboundInventoryActionRequest, ProtocolError>::failure(trailing.error());
  }

  ServerboundInventoryActionRequest request;
  request.inventory_id = InventoryID{inventory_id.value()};
  request.client_action_id = action_id.value();
  request.kind = kind.value();
  request.slot_index = slot.value();
  request.button = button.value();
  return Expected<ServerboundInventoryActionRequest, ProtocolError>::success(request);
}

std::vector<u8> serialize_clientbound_inventory_update(const ClientboundInventoryUpdate& update) {
  MessageWriter writer;
  writer.write<u32>(update.inventory_id.value);
  writer.write<u32>(update.revision);
  writer.write<u8>(update.selected_hotbar_slot);
  writer.write<u32>(static_cast<u32>(update.slots.size()));
  for (const std::optional<player::ItemStack>& slot : update.slots) {
    writer.write_bool(slot.has_value());
    if (slot.has_value()) {
      if (auto written = write_item_stack(writer, slot.value()); !written) {
        return {};
      }
    }
  }
  writer.write_bool(update.carried_stack.has_value());
  if (update.carried_stack.has_value()) {
    if (auto written = write_item_stack(writer, update.carried_stack.value()); !written) {
      return {};
    }
  }
  return std::move(writer).take_bytes();
}

Expected<ClientboundInventoryUpdate, ProtocolError> deserialize_clientbound_inventory_update(std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto inventory_id = reader.read<u32>();
  auto revision = reader.read<u32>();
  auto selected_slot = reader.read<u8>();
  auto slot_count = reader.read<u32>();
  if (!inventory_id || !revision || !selected_slot || !slot_count) {
    return Expected<ClientboundInventoryUpdate, ProtocolError>::failure(
        ProtocolError{"failed to decode inventory update header"});
  }
  if (slot_count.value() > MaxInventorySlots) {
    return Expected<ClientboundInventoryUpdate, ProtocolError>::failure(
        ProtocolError{"too many inventory slots"});
  }

  ClientboundInventoryUpdate update;
  update.inventory_id = InventoryID{inventory_id.value()};
  update.revision = revision.value();
  update.selected_hotbar_slot = selected_slot.value();
  update.slots.reserve(slot_count.value());
  for (u32 index = 0; index < slot_count.value(); ++index) {
    auto has_stack = reader.read_bool();
    if (!has_stack) {
      return Expected<ClientboundInventoryUpdate, ProtocolError>::failure(
          ProtocolError{"failed to decode inventory slot presence"});
    }
    if (!has_stack.value()) {
      update.slots.emplace_back(std::nullopt);
      continue;
    }
    auto stack = read_item_stack(reader);
    if (!stack) {
      return Expected<ClientboundInventoryUpdate, ProtocolError>::failure(stack.error());
    }
    update.slots.emplace_back(stack.value());
  }

  auto has_carried = reader.read_bool();
  if (!has_carried) {
    return Expected<ClientboundInventoryUpdate, ProtocolError>::failure(
        ProtocolError{"failed to decode carried inventory stack presence"});
  }
  if (has_carried.value()) {
    auto stack = read_item_stack(reader);
    if (!stack) {
      return Expected<ClientboundInventoryUpdate, ProtocolError>::failure(stack.error());
    }
    update.carried_stack = stack.value();
  }

  auto trailing = require_no_trailing_bytes(reader, "inventory message");
  if (!trailing) {
    return Expected<ClientboundInventoryUpdate, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundInventoryUpdate, ProtocolError>::success(std::move(update));
}

} // namespace arv::protocol
