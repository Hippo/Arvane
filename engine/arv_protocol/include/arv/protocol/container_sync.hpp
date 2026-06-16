#pragma once

#include "arv/core/coordinates.hpp"
#include "arv/core/domain_ids.hpp"
#include "arv/core/expected.hpp"
#include "arv/player/container.hpp"
#include "arv/player/inventory.hpp"
#include "arv/protocol/message_writer.hpp"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace arv::protocol {

enum class ContainerActionKind : u8 {
  SlotClick,
  ShiftTransfer,
  DropCursor,
  DropOneCursor,
};

enum class ContainerCloseReason : u8 {
  Closed,
  TooFar,
  ContainerRemoved,
  ChunkUnloaded,
  InvalidSession,
};

struct ServerboundOpenContainerBlock {
  WorldID world_id{};
  BlockPos block{};
  u32 client_action_id{};
};

struct ClientboundOpenContainer {
  u32 session_id{};
  u32 revision{};
  std::string container_type{};
  std::string title{};
  std::string ui_layout{};
  std::vector<std::optional<player::ItemStack>> container_slots{};
  std::vector<std::optional<player::ItemStack>> player_slots{};
  std::optional<player::ItemStack> carried_stack{};
};

struct ServerboundContainerAction {
  u32 session_id{};
  u32 client_action_id{};
  ContainerActionKind kind{ContainerActionKind::SlotClick};
  player::ContainerSlotSource source{player::ContainerSlotSource::OpenedContainer};
  u32 slot_index{};
  player::InventoryClickButton button{player::InventoryClickButton::Primary};
};

struct ClientboundContainerSetSlot {
  u32 session_id{};
  u32 revision{};
  player::ContainerSlotSource source{player::ContainerSlotSource::OpenedContainer};
  u32 slot_index{};
  std::optional<player::ItemStack> stack{};
  std::optional<player::ItemStack> carried_stack{};
};

struct ClientboundContainerSetContent {
  u32 session_id{};
  u32 revision{};
  std::vector<std::optional<player::ItemStack>> container_slots{};
  std::vector<std::optional<player::ItemStack>> player_slots{};
  std::optional<player::ItemStack> carried_stack{};
};

struct ServerboundCloseContainer {
  u32 session_id{};
  u32 client_action_id{};
};

struct ClientboundCloseContainer {
  u32 session_id{};
  ContainerCloseReason reason{ContainerCloseReason::Closed};
};

inline constexpr u32 MaxContainerSlots = 512;

std::vector<u8> serialize_serverbound_open_container_block(const ServerboundOpenContainerBlock& request);
Expected<ServerboundOpenContainerBlock, ProtocolError> deserialize_serverbound_open_container_block(
    std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_open_container(const ClientboundOpenContainer& packet);
Expected<ClientboundOpenContainer, ProtocolError> deserialize_clientbound_open_container(
    std::span<const u8> bytes);

std::vector<u8> serialize_serverbound_container_action(const ServerboundContainerAction& action);
Expected<ServerboundContainerAction, ProtocolError> deserialize_serverbound_container_action(
    std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_container_set_slot(const ClientboundContainerSetSlot& packet);
Expected<ClientboundContainerSetSlot, ProtocolError> deserialize_clientbound_container_set_slot(
    std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_container_set_content(const ClientboundContainerSetContent& packet);
Expected<ClientboundContainerSetContent, ProtocolError> deserialize_clientbound_container_set_content(
    std::span<const u8> bytes);

std::vector<u8> serialize_serverbound_close_container(const ServerboundCloseContainer& request);
Expected<ServerboundCloseContainer, ProtocolError> deserialize_serverbound_close_container(
    std::span<const u8> bytes);

std::vector<u8> serialize_clientbound_close_container(const ClientboundCloseContainer& packet);
Expected<ClientboundCloseContainer, ProtocolError> deserialize_clientbound_close_container(
    std::span<const u8> bytes);

} // namespace arv::protocol
