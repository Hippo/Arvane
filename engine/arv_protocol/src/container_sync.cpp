#include "arv/protocol/container_sync.hpp"

#include "arv/protocol/item_stack_wire.hpp"
#include "arv/protocol/message_primitives.hpp"

#include <utility>

namespace arv::protocol {
namespace {

[[nodiscard]] bool valid_slot_source(player::ContainerSlotSource source) noexcept {
  switch (source) {
    case player::ContainerSlotSource::OpenedContainer:
    case player::ContainerSlotSource::PlayerInventory:
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

[[nodiscard]] bool valid_action_kind(ContainerActionKind kind) noexcept {
  switch (kind) {
    case ContainerActionKind::SlotClick:
    case ContainerActionKind::ShiftTransfer:
    case ContainerActionKind::DropCursor:
    case ContainerActionKind::DropOneCursor:
      return true;
  }
  return false;
}

[[nodiscard]] bool valid_close_reason(ContainerCloseReason reason) noexcept {
  switch (reason) {
    case ContainerCloseReason::Closed:
    case ContainerCloseReason::TooFar:
    case ContainerCloseReason::ContainerRemoved:
    case ContainerCloseReason::ChunkUnloaded:
    case ContainerCloseReason::InvalidSession:
      return true;
  }
  return false;
}

void write_optional_stack(
    MessageWriter& writer,
    const std::optional<player::ItemStack>& stack) {
  writer.write_bool(stack.has_value());
  if (stack.has_value()) {
    (void)write_item_stack(writer, stack.value());
  }
}

[[nodiscard]] Expected<std::optional<player::ItemStack>, ProtocolError> read_optional_stack(
    MessageReader& reader) {
  auto has_stack = reader.read_bool();
  if (!has_stack) {
    return Expected<std::optional<player::ItemStack>, ProtocolError>::failure(has_stack.error());
  }
  if (!has_stack.value()) {
    return Expected<std::optional<player::ItemStack>, ProtocolError>::success(std::nullopt);
  }
  auto stack = read_item_stack(reader);
  if (!stack) {
    return Expected<std::optional<player::ItemStack>, ProtocolError>::failure(stack.error());
  }
  return Expected<std::optional<player::ItemStack>, ProtocolError>::success(stack.value());
}

void write_stack_slots(
    MessageWriter& writer,
    const std::vector<std::optional<player::ItemStack>>& slots) {
  writer.write<u32>(static_cast<u32>(slots.size()));
  for (const std::optional<player::ItemStack>& slot : slots) {
    write_optional_stack(writer, slot);
  }
}

[[nodiscard]] Expected<std::vector<std::optional<player::ItemStack>>, ProtocolError>
read_stack_slots(MessageReader& reader) {
  auto slot_count = reader.read<u32>();
  if (!slot_count) {
    return Expected<std::vector<std::optional<player::ItemStack>>, ProtocolError>::failure(slot_count.error());
  }
  if (slot_count.value() > MaxContainerSlots) {
    return Expected<std::vector<std::optional<player::ItemStack>>, ProtocolError>::failure(
        ProtocolError{"too many container slots"});
  }
  std::vector<std::optional<player::ItemStack>> slots;
  slots.reserve(slot_count.value());
  for (u32 index = 0; index < slot_count.value(); ++index) {
    auto stack = read_optional_stack(reader);
    if (!stack) {
      return Expected<std::vector<std::optional<player::ItemStack>>, ProtocolError>::failure(stack.error());
    }
    slots.push_back(std::move(stack.value()));
  }
  return Expected<std::vector<std::optional<player::ItemStack>>, ProtocolError>::success(
      std::move(slots));
}

} // namespace

std::vector<u8> serialize_serverbound_open_container_block(
    const ServerboundOpenContainerBlock& request) {
  MessageWriter writer;
  writer.write<u32>(request.world_id.value);
  write_block_pos(writer, request.block);
  writer.write<u32>(request.client_action_id);
  return std::move(writer).take_bytes();
}

Expected<ServerboundOpenContainerBlock, ProtocolError> deserialize_serverbound_open_container_block(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto world_id = reader.read<u32>();
  auto block = read_block_pos(reader);
  auto action_id = reader.read<u32>();
  if (!world_id || !block || !action_id) {
    return Expected<ServerboundOpenContainerBlock, ProtocolError>::failure(
        ProtocolError{"failed to decode open container block"});
  }
  auto trailing = require_no_trailing_bytes(reader, "open container block");
  if (!trailing) {
    return Expected<ServerboundOpenContainerBlock, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundOpenContainerBlock, ProtocolError>::success(
      {WorldID{world_id.value()}, block.value(), action_id.value()});
}

std::vector<u8> serialize_clientbound_open_container(
    const ClientboundOpenContainer& packet) {
  MessageWriter writer;
  writer.write<u32>(packet.session_id);
  writer.write<u32>(packet.revision);
  writer.write_string(packet.container_type);
  writer.write_string(packet.title);
  writer.write_string(packet.ui_layout);
  write_stack_slots(writer, packet.container_slots);
  write_stack_slots(writer, packet.player_slots);
  write_optional_stack(writer, packet.carried_stack);
  return std::move(writer).take_bytes();
}

Expected<ClientboundOpenContainer, ProtocolError> deserialize_clientbound_open_container(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto session_id = reader.read<u32>();
  auto revision = reader.read<u32>();
  auto container_type = reader.read_string();
  auto title = reader.read_string();
  auto ui_layout = reader.read_string();
  auto container_slots = read_stack_slots(reader);
  auto player_slots = read_stack_slots(reader);
  auto carried = read_optional_stack(reader);
  if (!session_id || !revision || !container_type || !title || !ui_layout ||
      !container_slots || !player_slots || !carried) {
    return Expected<ClientboundOpenContainer, ProtocolError>::failure(
        ProtocolError{"failed to decode open container"});
  }
  auto trailing = require_no_trailing_bytes(reader, "open container");
  if (!trailing) {
    return Expected<ClientboundOpenContainer, ProtocolError>::failure(trailing.error());
  }
  ClientboundOpenContainer packet;
  packet.session_id = session_id.value();
  packet.revision = revision.value();
  packet.container_type = std::move(container_type.value());
  packet.title = std::move(title.value());
  packet.ui_layout = std::move(ui_layout.value());
  packet.container_slots = std::move(container_slots.value());
  packet.player_slots = std::move(player_slots.value());
  packet.carried_stack = std::move(carried.value());
  return Expected<ClientboundOpenContainer, ProtocolError>::success(std::move(packet));
}

std::vector<u8> serialize_serverbound_container_action(
    const ServerboundContainerAction& action) {
  MessageWriter writer;
  writer.write<u32>(action.session_id);
  writer.write<u32>(action.client_action_id);
  writer.write<ContainerActionKind>(action.kind);
  writer.write<player::ContainerSlotSource>(action.source);
  writer.write<u32>(action.slot_index);
  writer.write<player::InventoryClickButton>(action.button);
  return std::move(writer).take_bytes();
}

Expected<ServerboundContainerAction, ProtocolError> deserialize_serverbound_container_action(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto session_id = reader.read<u32>();
  auto action_id = reader.read<u32>();
  auto kind = reader.read<ContainerActionKind>();
  auto source = reader.read<player::ContainerSlotSource>();
  auto slot = reader.read<u32>();
  auto button = reader.read<player::InventoryClickButton>();
  if (!session_id || !action_id || !kind || !source || !slot || !button) {
    return Expected<ServerboundContainerAction, ProtocolError>::failure(
        ProtocolError{"failed to decode container action"});
  }
  if (!valid_action_kind(kind.value()) ||
      !valid_slot_source(source.value()) ||
      !valid_click_button(button.value())) {
    return Expected<ServerboundContainerAction, ProtocolError>::failure(
        ProtocolError{"invalid container action enum value"});
  }
  auto trailing = require_no_trailing_bytes(reader, "container action");
  if (!trailing) {
    return Expected<ServerboundContainerAction, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundContainerAction, ProtocolError>::success(
      {session_id.value(), action_id.value(), kind.value(), source.value(), slot.value(), button.value()});
}

std::vector<u8> serialize_clientbound_container_set_slot(
    const ClientboundContainerSetSlot& packet) {
  MessageWriter writer;
  writer.write<u32>(packet.session_id);
  writer.write<u32>(packet.revision);
  writer.write<player::ContainerSlotSource>(packet.source);
  writer.write<u32>(packet.slot_index);
  write_optional_stack(writer, packet.stack);
  write_optional_stack(writer, packet.carried_stack);
  return std::move(writer).take_bytes();
}

Expected<ClientboundContainerSetSlot, ProtocolError> deserialize_clientbound_container_set_slot(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto session_id = reader.read<u32>();
  auto revision = reader.read<u32>();
  auto source = reader.read<player::ContainerSlotSource>();
  auto slot = reader.read<u32>();
  auto stack = read_optional_stack(reader);
  auto carried = read_optional_stack(reader);
  if (!session_id || !revision || !source || !slot || !stack || !carried) {
    return Expected<ClientboundContainerSetSlot, ProtocolError>::failure(
        ProtocolError{"failed to decode container set slot"});
  }
  if (!valid_slot_source(source.value())) {
    return Expected<ClientboundContainerSetSlot, ProtocolError>::failure(
        ProtocolError{"invalid container slot source"});
  }
  auto trailing = require_no_trailing_bytes(reader, "container set slot");
  if (!trailing) {
    return Expected<ClientboundContainerSetSlot, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundContainerSetSlot, ProtocolError>::success(
      {session_id.value(), revision.value(), source.value(), slot.value(), stack.value(), carried.value()});
}

std::vector<u8> serialize_clientbound_container_set_content(
    const ClientboundContainerSetContent& packet) {
  MessageWriter writer;
  writer.write<u32>(packet.session_id);
  writer.write<u32>(packet.revision);
  write_stack_slots(writer, packet.container_slots);
  write_stack_slots(writer, packet.player_slots);
  write_optional_stack(writer, packet.carried_stack);
  return std::move(writer).take_bytes();
}

Expected<ClientboundContainerSetContent, ProtocolError> deserialize_clientbound_container_set_content(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto session_id = reader.read<u32>();
  auto revision = reader.read<u32>();
  auto container_slots = read_stack_slots(reader);
  auto player_slots = read_stack_slots(reader);
  auto carried = read_optional_stack(reader);
  if (!session_id || !revision || !container_slots || !player_slots || !carried) {
    return Expected<ClientboundContainerSetContent, ProtocolError>::failure(
        ProtocolError{"failed to decode container content"});
  }
  auto trailing = require_no_trailing_bytes(reader, "container content");
  if (!trailing) {
    return Expected<ClientboundContainerSetContent, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundContainerSetContent, ProtocolError>::success(
      {session_id.value(), revision.value(), std::move(container_slots.value()), std::move(player_slots.value()), carried.value()});
}

std::vector<u8> serialize_serverbound_close_container(
    const ServerboundCloseContainer& request) {
  MessageWriter writer;
  writer.write<u32>(request.session_id);
  writer.write<u32>(request.client_action_id);
  return std::move(writer).take_bytes();
}

Expected<ServerboundCloseContainer, ProtocolError> deserialize_serverbound_close_container(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto session_id = reader.read<u32>();
  auto action_id = reader.read<u32>();
  if (!session_id || !action_id) {
    return Expected<ServerboundCloseContainer, ProtocolError>::failure(
        ProtocolError{"failed to decode close container"});
  }
  auto trailing = require_no_trailing_bytes(reader, "close container");
  if (!trailing) {
    return Expected<ServerboundCloseContainer, ProtocolError>::failure(trailing.error());
  }
  return Expected<ServerboundCloseContainer, ProtocolError>::success(
      {session_id.value(), action_id.value()});
}

std::vector<u8> serialize_clientbound_close_container(
    const ClientboundCloseContainer& packet) {
  MessageWriter writer;
  writer.write<u32>(packet.session_id);
  writer.write<ContainerCloseReason>(packet.reason);
  return std::move(writer).take_bytes();
}

Expected<ClientboundCloseContainer, ProtocolError> deserialize_clientbound_close_container(
    std::span<const u8> bytes) {
  MessageReader reader(bytes);
  auto session_id = reader.read<u32>();
  auto reason = reader.read<ContainerCloseReason>();
  if (!session_id || !reason) {
    return Expected<ClientboundCloseContainer, ProtocolError>::failure(
        ProtocolError{"failed to decode close container"});
  }
  if (!valid_close_reason(reason.value())) {
    return Expected<ClientboundCloseContainer, ProtocolError>::failure(
        ProtocolError{"invalid container close reason"});
  }
  auto trailing = require_no_trailing_bytes(reader, "close container");
  if (!trailing) {
    return Expected<ClientboundCloseContainer, ProtocolError>::failure(trailing.error());
  }
  return Expected<ClientboundCloseContainer, ProtocolError>::success(
      {session_id.value(), reason.value()});
}

} // namespace arv::protocol
