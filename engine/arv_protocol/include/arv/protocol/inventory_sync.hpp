#pragma once

#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/player/inventory.hpp"
#include "arv/protocol/message_writer.hpp"

#include <optional>
#include <span>
#include <vector>

namespace arv::protocol {

enum class InventoryActionKind : u8 {
  SlotClick,
  DropCursor,
  DropOneCursor,
  CloseMenu,
};

struct ServerboundInventoryActionRequest {
  InventoryID inventory_id{};
  u32 client_action_id{};
  InventoryActionKind kind{InventoryActionKind::SlotClick};
  u32 slot_index{};
  player::InventoryClickButton button{player::InventoryClickButton::Primary};
};

struct ClientboundInventoryUpdate {
  InventoryID inventory_id{};
  u32 revision{};
  u8 selected_hotbar_slot{};
  std::vector<std::optional<player::ItemStack>> slots{};
  std::optional<player::ItemStack> carried_stack{};
};

inline constexpr InventoryID PlayerInventoryID{1};
inline constexpr u32 MaxInventorySlots = 256;

std::vector<u8> serialize_serverbound_inventory_action_request(const ServerboundInventoryActionRequest& request);
Expected<ServerboundInventoryActionRequest, ProtocolError> deserialize_serverbound_inventory_action_request(
    std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_inventory_update(const ClientboundInventoryUpdate& update);
Expected<ClientboundInventoryUpdate, ProtocolError> deserialize_clientbound_inventory_update(std::span<const u8> bytes);

} // namespace arv::protocol
