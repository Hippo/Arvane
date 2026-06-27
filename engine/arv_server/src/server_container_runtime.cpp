#include "server_runtime_internal.hpp"

#include "arv/crafting/crafting.hpp"
#include "arv/protocol/container_sync.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>
#include <vector>

namespace arv::server::detail {
namespace {

constexpr std::string_view FurnaceContainerType = "arvane:furnace";
constexpr usize FurnaceInputSlot = 0U;
constexpr usize FurnaceFuelSlot = 1U;
constexpr usize FurnaceOutputSlot = 2U;
constexpr usize FurnaceSlotCount = 3U;
constexpr Seconds FurnaceTimeEpsilon = 0.0001F;

[[nodiscard]] Seconds server_time_seconds(TickIndex tick_index, TickRate base_tps) noexcept {
  return static_cast<Seconds>(tick_index) /
         static_cast<Seconds>(std::max<TickRate>(base_tps, 1U));
}

[[nodiscard]] bool same_container_source(
    const ContainerSession& session,
    WorldID world_id,
    BlockPos block) noexcept {
  return session.world_id == world_id && session.block == block;
}

[[nodiscard]] std::vector<u32> container_session_ids_at(
    const HashMap<u32, ContainerSession>& sessions,
    WorldID world_id,
    BlockPos block) {
  std::vector<u32> ids;
  for (const auto& [session_id, session] : sessions) {
    if (same_container_source(session, world_id, block)) {
      ids.push_back(session_id);
    }
  }
  return ids;
}

[[nodiscard]] std::vector<u32> container_session_ids_for_player_session(
    const HashMap<u32, ContainerSession>& sessions,
    SessionID player_session_id) {
  std::vector<u32> ids;
  for (const auto& [session_id, session] : sessions) {
    if (session.player_session_id == player_session_id) {
      ids.push_back(session_id);
    }
  }
  return ids;
}

[[nodiscard]] std::vector<std::optional<player::ItemStack>> inventory_slots(
    const player::Inventory& inventory) {
  return inventory.slots();
}

[[nodiscard]] bool player_can_reach_container_block(
    const player::PlayerState& player_state,
    const player::PlayerTuning& tuning,
    BlockPos block,
    f32 grace_blocks) noexcept {
  const f32 reach = std::max(0.0F, tuning.reach_distance + grace_blocks);
  const Vec3f eye = player_eye_position(player_state, tuning);
  return distance_squared(eye, block_center(block)) <= reach * reach;
}

[[nodiscard]] const voxel::BlockVariantRecord* loaded_container_variant_at(
    const world::WorldRuntime& world,
    const voxel::BlockRegistry* block_registry,
    WorldID world_id,
    BlockPos block) {
  if (block_registry == nullptr || world_id != world.id()) {
    return nullptr;
  }
  if (world.find_chunk(block_to_chunk(block)) == nullptr) {
    return nullptr;
  }
  const voxel::BlockVariantID variant_id = world.get_block(block);
  if (block_registry->is_air(variant_id)) {
    return nullptr;
  }
  const voxel::BlockVariantRecord* variant = block_registry->variant(variant_id);
  if (variant == nullptr || variant->container_type.empty()) {
    return nullptr;
  }
  return variant;
}

[[nodiscard]] protocol::ContainerCloseReason validate_container_session(
    const ContainerSession& container_session,
    const SessionRegistry& sessions,
    const world::WorldRuntime& world,
    const voxel::BlockRegistry* block_registry,
    const player::ContainerTypeRegistry* container_types,
    const player::PlayerTuning& tuning,
    f32 reach_grace_blocks) {
  const ServerSession* player_session = sessions.find(container_session.player_session_id);
  if (player_session == nullptr || !player_session->authenticated || player_session->closing ||
      player_session->player_id != container_session.player_id ||
      player_session->world_id != container_session.world_id) {
    return protocol::ContainerCloseReason::InvalidSession;
  }

  const player::PlayerState* player_state = world.find_player(container_session.player_id);
  if (player_state == nullptr || player_state->world_id != container_session.world_id) {
    return protocol::ContainerCloseReason::InvalidSession;
  }

  if (world.find_chunk(block_to_chunk(container_session.block)) == nullptr) {
    return protocol::ContainerCloseReason::ChunkUnloaded;
  }

  const voxel::BlockVariantRecord* variant = loaded_container_variant_at(
      world,
      block_registry,
      container_session.world_id,
      container_session.block);
  if (variant == nullptr || variant->container_type != container_session.container_type) {
    return protocol::ContainerCloseReason::ContainerRemoved;
  }

  if (container_types == nullptr ||
      container_types->find(container_session.container_type) == nullptr ||
      world.find_block_container(container_session.block) == nullptr) {
    return protocol::ContainerCloseReason::ContainerRemoved;
  }

  if (!player_can_reach_container_block(
          *player_state,
          tuning,
          container_session.block,
          reach_grace_blocks)) {
    return protocol::ContainerCloseReason::TooFar;
  }

  return protocol::ContainerCloseReason::Closed;
}

[[nodiscard]] player::Inventory* inventory_for_source(
    player::ContainerSlotSource source,
    player::PlayerState& player_state,
    world::BlockAttachedContainer& container) noexcept {
  switch (source) {
    case player::ContainerSlotSource::OpenedContainer:
      return &container.inventory;
    case player::ContainerSlotSource::PlayerInventory:
      return &player_state.inventory;
  }
  return nullptr;
}

[[nodiscard]] player::Inventory* destination_for_shift_transfer(
    player::ContainerSlotSource source,
    player::PlayerState& player_state,
    world::BlockAttachedContainer& container) noexcept {
  switch (source) {
    case player::ContainerSlotSource::OpenedContainer:
      return &player_state.inventory;
    case player::ContainerSlotSource::PlayerInventory:
      return &container.inventory;
  }
  return nullptr;
}

[[nodiscard]] bool shift_transfer_stack(
    player::Inventory& source,
    usize source_slot,
    player::Inventory& destination,
    const player::ItemRegistry& item_registry) {
  const std::optional<player::ItemStack> source_stack = source.slot(source_slot);
  if (!source_stack.has_value()) {
    return false;
  }
  (void)source.clear_slot(source_slot);
  const player::InventoryInsertResult insert_result =
      destination.insert_stack(source_stack.value(), item_registry);
  if (insert_result.leftover.has_value()) {
    (void)source.set_slot(source_slot, insert_result.leftover);
  }
  return !insert_result.leftover.has_value() ||
         insert_result.leftover->count != source_stack->count;
}

[[nodiscard]] bool block_container_is_furnace(
    const world::BlockAttachedContainer& container) noexcept {
  return container.container_type == FurnaceContainerType &&
         container.inventory.slot_count() >= FurnaceSlotCount;
}

[[nodiscard]] bool is_furnace_fuel(
    player::ItemID item,
    const player::ItemRegistry& item_registry) noexcept {
  return crafting::item_has_tag(item_registry, item, crafting::SmeltingFuelItemTag);
}

[[nodiscard]] bool has_furnace_fuel(
    const player::Inventory& inventory,
    const player::ItemRegistry& item_registry) noexcept {
  const std::optional<player::ItemStack>& fuel = inventory.slot(FurnaceFuelSlot);
  return fuel.has_value() && fuel->count > 0U &&
         is_furnace_fuel(fuel->item, item_registry);
}

[[nodiscard]] bool consume_furnace_fuel(
    player::Inventory& inventory,
    const player::ItemRegistry& item_registry) {
  return has_furnace_fuel(inventory, item_registry) &&
         inventory.consume_one(FurnaceFuelSlot);
}

[[nodiscard]] player::ItemStack furnace_output_stack(
    player::ItemID item,
    u16 count) {
  return player::ItemStack{item, count};
}

[[nodiscard]] bool can_accept_furnace_output(
    const player::Inventory& inventory,
    player::ItemID output,
    u16 output_count,
    const player::ItemRegistry& item_registry) {
  const player::ItemRecord* item = item_registry.item(output);
  if (item == nullptr || output_count == 0U || output_count > item->max_stack_size) {
    return false;
  }
  const std::optional<player::ItemStack>& slot = inventory.slot(FurnaceOutputSlot);
  if (!slot.has_value()) {
    return true;
  }
  const player::ItemStack produced = furnace_output_stack(output, output_count);
  return player::item_stacks_compatible(slot.value(), produced) &&
         slot->count <= item->max_stack_size - output_count;
}

[[nodiscard]] bool add_furnace_output(
    player::Inventory& inventory,
    player::ItemID output,
    u16 output_count,
    const player::ItemRegistry& item_registry) {
  if (!can_accept_furnace_output(inventory, output, output_count, item_registry)) {
    return false;
  }
  const std::optional<player::ItemStack>& slot = inventory.slot(FurnaceOutputSlot);
  if (!slot.has_value()) {
    return inventory.set_slot(
        FurnaceOutputSlot,
        furnace_output_stack(output, output_count));
  }
  player::ItemStack merged = slot.value();
  merged.count = static_cast<u16>(merged.count + output_count);
  return inventory.set_slot(FurnaceOutputSlot, merged);
}

[[nodiscard]] const crafting::SmeltingRule* smelting_rule_for_input(
    const player::Inventory& inventory,
    const crafting::SmeltingRuleRegistry& registry) noexcept {
  const std::optional<player::ItemStack>& input = inventory.slot(FurnaceInputSlot);
  return input.has_value() ? registry.find_by_input(input->item) : nullptr;
}

[[nodiscard]] bool can_start_furnace_cycle(
    const player::Inventory& inventory,
    const crafting::SmeltingRule& rule,
    const player::ItemRegistry& item_registry) {
  const std::optional<player::ItemStack>& input = inventory.slot(FurnaceInputSlot);
  if (!input.has_value() || input->item != rule.input || input->count < rule.input_count) {
    return false;
  }
  return has_furnace_fuel(inventory, item_registry) &&
         can_accept_furnace_output(
             inventory,
             rule.output,
             rule.output_count,
             item_registry);
}

[[nodiscard]] bool start_furnace_cycle(
    world::BlockAttachedContainer& container,
    const crafting::SmeltingRule& rule,
    Seconds start_at,
    const player::ItemRegistry& item_registry) {
  if (!can_start_furnace_cycle(container.inventory, rule, item_registry)) {
    return false;
  }
  if (!consume_furnace_fuel(container.inventory, item_registry)) {
    return false;
  }
  if (!container.inventory.remove_amount(FurnaceInputSlot, rule.input_count).has_value()) {
    return false;
  }
  container.furnace.active_cycle = world::FurnaceCycleState{
      rule.input,
      rule.input_count,
      rule.output,
      rule.output_count,
      rule.duration_secs,
      start_at};
  return true;
}

[[nodiscard]] bool try_start_furnace_cycle_from_input(
    world::BlockAttachedContainer& container,
    const crafting::SmeltingRuleRegistry& registry,
    const player::ItemRegistry& item_registry,
    Seconds start_at) {
  const crafting::SmeltingRule* rule =
      smelting_rule_for_input(container.inventory, registry);
  return rule != nullptr &&
         start_furnace_cycle(container, *rule, start_at, item_registry);
}

[[nodiscard]] bool resolve_furnace_smelting(
    world::BlockAttachedContainer& container,
    const crafting::SmeltingRuleRegistry* smelting_registry,
    const player::ItemRegistry* item_registry,
    Seconds now_seconds) {
  if (!block_container_is_furnace(container) ||
      smelting_registry == nullptr ||
      item_registry == nullptr) {
    return false;
  }

  bool changed = false;
  while (container.furnace.active_cycle.has_value()) {
    world::FurnaceCycleState& cycle = container.furnace.active_cycle.value();
    if (cycle.duration_secs <= 0.0F ||
        !std::isfinite(cycle.duration_secs) ||
        !std::isfinite(cycle.start_at)) {
      container.furnace.active_cycle.reset();
      changed = true;
      break;
    }
    if (now_seconds + FurnaceTimeEpsilon < cycle.start_at + cycle.duration_secs) {
      break;
    }
    if (!can_accept_furnace_output(
            container.inventory,
            cycle.output,
            cycle.output_count,
            *item_registry)) {
      break;
    }

    if (!add_furnace_output(
            container.inventory,
            cycle.output,
            cycle.output_count,
            *item_registry)) {
      break;
    }
    const Seconds next_start_at = cycle.start_at + cycle.duration_secs;
    changed = true;

    if (!try_start_furnace_cycle_from_input(
            container,
            *smelting_registry,
            *item_registry,
            next_start_at)) {
      container.furnace.active_cycle.reset();
      break;
    }
  }

  if (!container.furnace.active_cycle.has_value() &&
      try_start_furnace_cycle_from_input(
          container,
          *smelting_registry,
          *item_registry,
          now_seconds)) {
    changed = true;
  }
  return changed;
}

[[nodiscard]] bool shift_transfer_stack_to_slot(
    player::Inventory& source,
    usize source_slot,
    player::Inventory& destination,
    usize destination_slot,
    const player::ItemRegistry& item_registry) {
  const std::optional<player::ItemStack> source_stack = source.slot(source_slot);
  if (!source_stack.has_value()) {
    return false;
  }
  (void)source.clear_slot(source_slot);
  player::ItemStack moving = source_stack.value();
  const player::InventoryOperationResult result =
      destination.merge_stack_into_slot(destination_slot, moving, item_registry);
  if (moving.count > 0U) {
    (void)source.set_slot(source_slot, moving);
  }
  return result.moved_count > 0U;
}

[[nodiscard]] bool shift_transfer_stack_into_furnace(
    player::Inventory& source,
    usize source_slot,
    world::BlockAttachedContainer& container,
    const player::ItemRegistry& item_registry,
    const crafting::SmeltingRuleRegistry* smelting_registry) {
  const std::optional<player::ItemStack> source_stack = source.slot(source_slot);
  if (!source_stack.has_value()) {
    return false;
  }
  const bool smeltable =
      smelting_registry != nullptr &&
      smelting_registry->find_by_input(source_stack->item) != nullptr;
  const bool fuel = is_furnace_fuel(source_stack->item, item_registry);
  if (!smeltable && !fuel) {
    return false;
  }
  return shift_transfer_stack_to_slot(
      source,
      source_slot,
      container.inventory,
      smeltable ? FurnaceInputSlot : FurnaceFuelSlot,
      item_registry);
}

[[nodiscard]] bool furnace_slot_accepts_carried_stack(
    usize slot_index,
    const std::optional<player::ItemStack>& carried_stack,
    const player::ItemRegistry& item_registry,
    const crafting::SmeltingRuleRegistry* smelting_registry) noexcept {
  if (!carried_stack.has_value()) {
    return true;
  }
  if (slot_index == FurnaceInputSlot) {
    return smelting_registry != nullptr &&
           smelting_registry->find_by_input(carried_stack->item) != nullptr;
  }
  if (slot_index == FurnaceFuelSlot) {
    return is_furnace_fuel(carried_stack->item, item_registry);
  }
  return true;
}

[[nodiscard]] bool apply_furnace_output_slot_click(
    world::BlockAttachedContainer& container,
    player::InventoryClickButton button,
    std::optional<player::ItemStack>& carried_stack,
    const player::ItemRegistry& item_registry) {
  std::optional<player::ItemStack> output_stack =
      container.inventory.slot(FurnaceOutputSlot);
  if (!output_stack.has_value()) {
    return false;
  }

  if (!carried_stack.has_value()) {
    const u16 taken =
        button == player::InventoryClickButton::Primary
            ? output_stack->count
            : static_cast<u16>((output_stack->count + 1U) / 2U);
    carried_stack = container.inventory.remove_amount(FurnaceOutputSlot, taken);
    return carried_stack.has_value();
  }

  if (!player::item_stacks_compatible(carried_stack.value(), output_stack.value())) {
    return false;
  }
  const player::ItemRecord* item = item_registry.item(output_stack->item);
  if (item == nullptr || carried_stack->count >= item->max_stack_size) {
    return false;
  }

  const u16 capacity = static_cast<u16>(item->max_stack_size - carried_stack->count);
  const u16 moved =
      button == player::InventoryClickButton::Primary
          ? std::min(capacity, output_stack->count)
          : std::min<u16>(capacity, 1U);
  if (moved == 0U) {
    return false;
  }
  carried_stack->count = static_cast<u16>(carried_stack->count + moved);
  (void)container.inventory.remove_amount(FurnaceOutputSlot, moved);
  return true;
}

} // namespace

void ServerRuntimeImpl::handle_open_container_block(
    net::ConnectionID connection_id,
    const protocol::ServerboundOpenContainerBlock& request) {
  ServerSession* session = sessions_.find_by_connection(connection_id);
  if (session == nullptr || !session->authenticated || session->closing ||
      request.world_id != session->world_id ||
      container_types_ == nullptr || item_registry_ == nullptr) {
    return;
  }
  world::WorldRuntime* runtime = world_by_id(request.world_id);
  if (runtime == nullptr) {
    return;
  }

  const player::PlayerState* player_state = runtime->find_player(session->player_id);
  if (player_state == nullptr || player_state->world_id != request.world_id) {
    return;
  }
  if (!player_can_reach_container_block(
          *player_state,
          player_tuning_,
          request.block,
          0.0F)) {
    return;
  }

  const voxel::BlockVariantRecord* variant = loaded_container_variant_at(
      *runtime,
      block_registry_,
      request.world_id,
      request.block);
  if (variant == nullptr) {
    return;
  }

  const player::ContainerTypeDefinition* container_type =
      container_types_->find(variant->container_type);
  if (container_type == nullptr || container_type->slot_count == 0U ||
      container_type->slot_count > protocol::MaxContainerSlots) {
    return;
  }
  if (container_layouts_ != nullptr &&
      container_layouts_->find(container_type->ui_layout) == nullptr) {
    return;
  }

  world::BlockAttachedContainer* container = runtime->ensure_block_container(
      request.block,
      container_type->id,
      container_type->slot_count);
  if (container == nullptr) {
    return;
  }
  if (resolve_furnace_smelting(
          *container,
          smelting_registry_,
          item_registry_,
          server_time_seconds(tick_index_, base_tps_))) {
    send_container_content_to_viewers(request.world_id, request.block);
  }

  close_container_sessions_for_player_session(
      session->session_id,
      protocol::ContainerCloseReason::Closed);

  u32 container_session_id = next_container_session_id_++;
  if (container_session_id == 0U) {
    container_session_id = next_container_session_id_++;
  }
  ContainerSession container_session;
  container_session.session_id = container_session_id;
  container_session.player_session_id = session->session_id;
  container_session.player_id = session->player_id;
  container_session.world_id = request.world_id;
  container_session.block = request.block;
  container_session.container_type = container_type->id;
  container_session.revision = 1U;
  auto [iter, inserted] =
      container_sessions_.emplace(container_session_id, std::move(container_session));
  if (!inserted) {
    return;
  }

  const protocol::ClientboundOpenContainer packet{
      iter->second.session_id,
      iter->second.revision,
      container_type->id,
      container_type->title,
      container_type->ui_layout,
      inventory_slots(container->inventory),
      inventory_slots(player_state->inventory),
      player_state->inventory_cursor_stack,
      protocol::ContainerBlockSource{request.world_id, request.block}};
  (void)send_protocol_message(
      *transport_,
      session->connection_id,
      protocol::MessageType::ClientboundOpenContainer,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_open_container(packet));
}

void ServerRuntimeImpl::handle_container_action(
    net::ConnectionID connection_id,
    const protocol::ServerboundContainerAction& action) {
  ServerSession* player_session = sessions_.find_by_connection(connection_id);
  auto session_iter = container_sessions_.find(action.session_id);
  if (player_session == nullptr || !player_session->authenticated ||
      player_session->closing || session_iter == container_sessions_.end() ||
      session_iter->second.player_session_id != player_session->session_id ||
      item_registry_ == nullptr) {
    return;
  }

  ContainerSession& container_session = session_iter->second;
  world::WorldRuntime* runtime = world_by_id(container_session.world_id);
  if (runtime == nullptr) {
    close_container_session(
        container_session.session_id,
        protocol::ContainerCloseReason::InvalidSession);
    return;
  }
  const protocol::ContainerCloseReason validation = validate_container_session(
      container_session,
      sessions_,
      *runtime,
      block_registry_,
      container_types_,
      player_tuning_,
      0.75F);
  if (validation != protocol::ContainerCloseReason::Closed) {
    close_container_session(container_session.session_id, validation);
    return;
  }

  player::PlayerState* player_state = runtime->find_player(container_session.player_id);
  world::BlockAttachedContainer* container =
      runtime->find_block_container(container_session.block);
  if (player_state == nullptr || container == nullptr) {
    close_container_session(
        container_session.session_id,
        protocol::ContainerCloseReason::ContainerRemoved);
    return;
  }

  const Seconds now_seconds = server_time_seconds(tick_index_, base_tps_);
  bool changed = resolve_furnace_smelting(
      *container,
      smelting_registry_,
      item_registry_,
      now_seconds);
  switch (action.kind) {
    case protocol::ContainerActionKind::SlotClick: {
      if (block_container_is_furnace(*container) &&
          action.source == player::ContainerSlotSource::OpenedContainer &&
          action.slot_index == FurnaceOutputSlot) {
        changed =
            apply_furnace_output_slot_click(
                *container,
                action.button,
                player_state->inventory_cursor_stack,
                *item_registry_) ||
            changed;
      } else if (player::Inventory* inventory =
                     inventory_for_source(action.source, *player_state, *container);
                 inventory != nullptr) {
        if (block_container_is_furnace(*container) &&
            action.source == player::ContainerSlotSource::OpenedContainer &&
            !furnace_slot_accepts_carried_stack(
                action.slot_index,
                player_state->inventory_cursor_stack,
                *item_registry_,
                smelting_registry_)) {
          break;
        }
        const player::InventoryOperationResult result =
            player::apply_inventory_slot_click(
                *inventory,
                action.slot_index,
                action.button,
                player_state->inventory_cursor_stack,
                *item_registry_);
        changed = result.accepted() || changed;
      }
      break;
    }
    case protocol::ContainerActionKind::ShiftTransfer: {
      player::Inventory* source =
          inventory_for_source(action.source, *player_state, *container);
      player::Inventory* destination =
          destination_for_shift_transfer(action.source, *player_state, *container);
      if (source != nullptr && destination != nullptr &&
          source->is_valid_slot(action.slot_index)) {
        if (block_container_is_furnace(*container) &&
            action.source == player::ContainerSlotSource::PlayerInventory) {
          changed =
              shift_transfer_stack_into_furnace(
                  *source,
                  action.slot_index,
                  *container,
                  *item_registry_,
                  smelting_registry_) ||
              changed;
        } else {
          changed =
              shift_transfer_stack(
                  *source,
                  action.slot_index,
                  *destination,
                  *item_registry_) ||
              changed;
        }
      }
      break;
    }
    case protocol::ContainerActionKind::DropCursor:
      changed =
          drop_player_inventory_cursor(player_state->player_id).has_value() ||
          changed;
      break;
    case protocol::ContainerActionKind::DropOneCursor:
      changed =
          drop_one_from_player_inventory_cursor(player_state->player_id).has_value() ||
          changed;
      break;
  }

  changed = resolve_furnace_smelting(
                *container,
                smelting_registry_,
                item_registry_,
                now_seconds) ||
            changed;
  if (changed) {
    send_container_content_to_viewers(
        container_session.world_id,
        container_session.block);
  } else {
    send_container_content(container_session);
  }
}

void ServerRuntimeImpl::handle_close_container(
    net::ConnectionID connection_id,
    const protocol::ServerboundCloseContainer& request) {
  ServerSession* player_session = sessions_.find_by_connection(connection_id);
  auto session_iter = container_sessions_.find(request.session_id);
  if (player_session == nullptr || session_iter == container_sessions_.end() ||
      session_iter->second.player_session_id != player_session->session_id) {
    return;
  }
  close_container_session(
      request.session_id,
      protocol::ContainerCloseReason::Closed);
}

void ServerRuntimeImpl::tick_container_sessions() {
  std::vector<std::pair<u32, protocol::ContainerCloseReason>> sessions_to_close;
  for (const auto& [session_id, container_session] : container_sessions_) {
    const world::WorldRuntime* runtime = world_by_id(container_session.world_id);
    if (runtime == nullptr) {
      sessions_to_close.emplace_back(
          session_id,
          protocol::ContainerCloseReason::InvalidSession);
      continue;
    }
    const protocol::ContainerCloseReason validation = validate_container_session(
        container_session,
        sessions_,
        *runtime,
        block_registry_,
        container_types_,
        player_tuning_,
        0.75F);
    if (validation != protocol::ContainerCloseReason::Closed) {
      sessions_to_close.emplace_back(session_id, validation);
    }
  }

  for (const auto& [session_id, reason] : sessions_to_close) {
    close_container_session(session_id, reason);
  }
}

void ServerRuntimeImpl::close_container_session(
    u32 container_session_id,
    protocol::ContainerCloseReason reason,
    bool return_cursor) {
  auto iter = container_sessions_.find(container_session_id);
  if (iter == container_sessions_.end()) {
    return;
  }
  const ContainerSession session = iter->second;
  container_sessions_.erase(iter);

  if (return_cursor) {
    (void)close_player_inventory_menu(session.player_id);
  }

  const ServerSession* player_session = sessions_.find(session.player_session_id);
  if (player_session == nullptr || player_session->closing || !player_session->authenticated ||
      transport_ == nullptr) {
    return;
  }

  const protocol::ClientboundCloseContainer packet{session.session_id, reason};
  (void)send_protocol_message(
      *transport_,
      player_session->connection_id,
      protocol::MessageType::ClientboundCloseContainer,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_close_container(packet));
}

void ServerRuntimeImpl::close_container_sessions_for_player_session(
    SessionID session_id,
    protocol::ContainerCloseReason reason) {
  const std::vector<u32> sessions_to_close =
      container_session_ids_for_player_session(container_sessions_, session_id);
  for (const u32 container_session_id : sessions_to_close) {
    close_container_session(container_session_id, reason);
  }
}

void ServerRuntimeImpl::close_container_sessions_at(
    WorldID world_id,
    BlockPos block,
    protocol::ContainerCloseReason reason) {
  const std::vector<u32> sessions_to_close =
      container_session_ids_at(container_sessions_, world_id, block);
  for (const u32 container_session_id : sessions_to_close) {
    close_container_session(container_session_id, reason);
  }
}

void ServerRuntimeImpl::drop_and_remove_block_container(WorldID world_id, BlockPos block) {
  world::WorldRuntime* runtime = world_by_id(world_id);
  if (runtime == nullptr) {
    return;
  }

  close_container_sessions_at(
      world_id,
      block,
      protocol::ContainerCloseReason::ContainerRemoved);

  world::BlockAttachedContainer* container = runtime->find_block_container(block);
  if (container == nullptr) {
    return;
  }

  const Vec3f drop_position = block_center(block);
  for (const std::optional<player::ItemStack>& stack : container->inventory.slots()) {
    if (stack.has_value() && stack->count > 0U) {
      spawn_dropped_item_stack(world_id, drop_position, stack.value());
    }
  }
  (void)runtime->remove_block_container(block);
}

void ServerRuntimeImpl::send_container_content(ContainerSession& session) {
  const ServerSession* player_session = sessions_.find(session.player_session_id);
  const world::WorldRuntime* runtime = world_by_id(session.world_id);
  const player::PlayerState* player_state =
      runtime == nullptr ? nullptr : runtime->find_player(session.player_id);
  const world::BlockAttachedContainer* container =
      runtime == nullptr ? nullptr : runtime->find_block_container(session.block);
  if (player_session == nullptr || player_state == nullptr || container == nullptr ||
      player_session->closing || !player_session->authenticated || transport_ == nullptr) {
    return;
  }

  ++session.revision;
  const protocol::ClientboundContainerSetContent packet{
      session.session_id,
      session.revision,
      inventory_slots(container->inventory),
      inventory_slots(player_state->inventory),
      player_state->inventory_cursor_stack};
  (void)send_protocol_message(
      *transport_,
      player_session->connection_id,
      protocol::MessageType::ClientboundContainerSetContent,
      protocol::ReliabilityClass::ReliableOrdered,
      protocol::NetworkLane::Gameplay,
      protocol::serialize_clientbound_container_set_content(packet));
}

void ServerRuntimeImpl::send_container_content_to_viewers(WorldID world_id, BlockPos block) {
  const std::vector<u32> viewer_ids =
      container_session_ids_at(container_sessions_, world_id, block);
  for (const u32 session_id : viewer_ids) {
    auto iter = container_sessions_.find(session_id);
    if (iter != container_sessions_.end()) {
      send_container_content(iter->second);
    }
  }
}

} // namespace arv::server::detail
