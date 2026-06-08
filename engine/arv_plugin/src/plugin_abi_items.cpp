#include "plugin_manager_internal.hpp"

#include "arv/io/data_codecs.hpp"

#include <utility>

namespace arv::plugin {
namespace {

[[nodiscard]] bool valid_location_kind(ArvItemStackLocationKind kind) noexcept {
  switch (kind) {
    case ARV_ITEM_STACK_SELECTED_HOTBAR:
    case ARV_ITEM_STACK_INVENTORY_SLOT:
    case ARV_ITEM_STACK_CURSOR:
      return true;
  }
  return false;
}

[[nodiscard]] std::optional<player::ItemStack> stack_from_location(
    player::PlayerState& player_state,
    ArvItemStackLocation location,
    std::string& error) {
  if (!valid_location_kind(location.kind)) {
    error = "invalid item stack location kind";
    return std::nullopt;
  }

  switch (location.kind) {
    case ARV_ITEM_STACK_SELECTED_HOTBAR: {
      if (!player_state.inventory.is_valid_slot(player_state.selected_hotbar_slot)) {
        error = "selected hotbar slot is invalid";
        return std::nullopt;
      }
      return player_state.inventory.slot(player_state.selected_hotbar_slot);
    }
    case ARV_ITEM_STACK_INVENTORY_SLOT: {
      if (!player_state.inventory.is_valid_slot(location.slot_index)) {
        error = "inventory slot is invalid";
        return std::nullopt;
      }
      return player_state.inventory.slot(location.slot_index);
    }
    case ARV_ITEM_STACK_CURSOR:
      return player_state.inventory_cursor_stack;
  }

  error = "invalid item stack location kind";
  return std::nullopt;
}

[[nodiscard]] bool location_is_valid(
    const player::PlayerState& player_state,
    ArvItemStackLocation location) noexcept {
  switch (location.kind) {
    case ARV_ITEM_STACK_SELECTED_HOTBAR:
      return player_state.inventory.is_valid_slot(player_state.selected_hotbar_slot);
    case ARV_ITEM_STACK_INVENTORY_SLOT:
      return player_state.inventory.is_valid_slot(location.slot_index);
    case ARV_ITEM_STACK_CURSOR:
      return true;
  }
  return false;
}

[[nodiscard]] bool store_stack_at_location(
    player::PlayerState& player_state,
    ArvItemStackLocation location,
    player::ItemStack stack) {
  player::normalize_item_stack_data(stack);
  switch (location.kind) {
    case ARV_ITEM_STACK_SELECTED_HOTBAR:
      return player_state.inventory.set_slot(player_state.selected_hotbar_slot, std::move(stack));
    case ARV_ITEM_STACK_INVENTORY_SLOT:
      return player_state.inventory.set_slot(location.slot_index, std::move(stack));
    case ARV_ITEM_STACK_CURSOR:
      player_state.inventory_cursor_stack = std::move(stack);
      return true;
  }
  return false;
}

[[nodiscard]] bool stack_data_within_limit(const std::optional<io::DataDocument>& data) {
  if (!data.has_value()) {
    return true;
  }
  auto encoded = io::encode_binary(data.value());
  return encoded && encoded.value().size() <= player::MaxItemStackDataBytes;
}

[[nodiscard]] ArvResult validate_namespace_write(
    PluginCallContext* ctx,
    ArvStringView name_space,
    std::string_view api_name,
    std::string& out_namespace) {
  if (ArvResult sync = require_sync_context(ctx, api_name); sync.code != ARV_RESULT_OK) {
    return sync;
  }
  out_namespace = abi_string(name_space);
  if (out_namespace.empty()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "namespace is empty");
  }
  if (!owns_namespace(ctx->info(), out_namespace)) {
    return make_result(
        ctx,
        ARV_RESULT_PERMISSION_DENIED,
        "plugin does not own item data namespace");
  }
  return ok(ctx);
}

[[nodiscard]] ArvResult validate_namespace_read(
    PluginCallContext* ctx,
    ArvStringView name_space,
    std::string_view api_name,
    std::string& out_namespace) {
  if (ArvResult sync = require_sync_context(ctx, api_name); sync.code != ARV_RESULT_OK) {
    return sync;
  }
  out_namespace = abi_string(name_space);
  if (out_namespace.empty()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "namespace is empty");
  }
  return ok(ctx);
}

[[nodiscard]] player::PlayerState* require_player(
    PluginCallContext* ctx,
    ArvItemStackLocation location) {
  return player_from_id(ctx, location.player);
}

ArvResult api_item_stack_info(
    void* context,
    ArvItemStackLocation location,
    ArvItemStackInfo* out_info) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "items.stack_info");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_info == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_info is null");
  }
  player::PlayerState* player_state = require_player(ctx, location);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  if (!location_is_valid(*player_state, location)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "item stack location is invalid");
  }

  std::string error;
  const std::optional<player::ItemStack> stack =
      stack_from_location(*player_state, location, error);
  ArvItemStackInfo info{};
  if (stack.has_value()) {
    info.present = true;
    info.item_id = stack->item.value;
    info.count = stack->count;
    info.has_data = player::item_stack_has_data(stack.value());
    if (ctx->impl->runtime.item_registry != nullptr) {
      if (const player::ItemRecord* item =
              ctx->impl->runtime.item_registry->item(stack->item);
          item != nullptr) {
        info.item_name = stable_view(item->name);
      }
    }
  }
  *out_info = info;
  return ok(ctx);
}

ArvResult api_load_stack_data(
    void* context,
    ArvItemStackLocation location,
    ArvDataDocumentHandle* out_document) {
  PluginCallContext* ctx = call_context(context);
  if (ArvResult sync = require_sync_context(ctx, "items.load_stack_data");
      sync.code != ARV_RESULT_OK) {
    return sync;
  }
  if (out_document == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_document is null");
  }
  player::PlayerState* player_state = require_player(ctx, location);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  if (!location_is_valid(*player_state, location)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "item stack location is invalid");
  }

  std::string error;
  const std::optional<player::ItemStack> stack =
      stack_from_location(*player_state, location, error);
  if (!stack.has_value()) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "item stack is empty");
  }

  *out_document = store_document(
      ctx,
      stack->data.value_or(io::DataDocument{1U, io::DataValue::object()}));
  return ok(ctx);
}

ArvResult api_load_stack_namespace_data(
    void* context,
    ArvItemStackLocation location,
    ArvStringView name_space,
    ArvDataDocumentHandle* out_document) {
  PluginCallContext* ctx = call_context(context);
  std::string namespace_text;
  if (ArvResult valid = validate_namespace_read(
          ctx,
          name_space,
          "items.load_stack_namespace_data",
          namespace_text);
      valid.code != ARV_RESULT_OK) {
    return valid;
  }
  if (out_document == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "out_document is null");
  }
  player::PlayerState* player_state = require_player(ctx, location);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  if (!location_is_valid(*player_state, location)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "item stack location is invalid");
  }

  std::string error;
  const std::optional<player::ItemStack> stack =
      stack_from_location(*player_state, location, error);
  if (!stack.has_value()) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "item stack is empty");
  }

  io::DataDocument document{1U, io::DataValue::object()};
  if (stack->data.has_value()) {
    if (const io::DataValue* namespace_data = stack->data->root.get(namespace_text);
        namespace_data != nullptr) {
      document.root = *namespace_data;
    }
  }
  *out_document = store_document(ctx, std::move(document));
  return ok(ctx);
}

ArvResult api_save_stack_namespace_data(
    void* context,
    ArvItemStackLocation location,
    ArvStringView name_space,
    ArvDataDocumentHandle document_handle) {
  PluginCallContext* ctx = call_context(context);
  std::string namespace_text;
  if (ArvResult valid = validate_namespace_write(
          ctx,
          name_space,
          "items.save_stack_namespace_data",
          namespace_text);
      valid.code != ARV_RESULT_OK) {
    return valid;
  }
  const io::DataDocument* document = document_from_handle(ctx, document_handle);
  if (document == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "item data document is invalid");
  }
  player::PlayerState* player_state = require_player(ctx, location);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  if (!location_is_valid(*player_state, location)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "item stack location is invalid");
  }

  std::string error;
  std::optional<player::ItemStack> stack =
      stack_from_location(*player_state, location, error);
  if (!stack.has_value()) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "item stack is empty");
  }

  if (!stack->data.has_value() || stack->data->root.kind() != io::DataKind::Object) {
    stack->data = io::DataDocument{1U, io::DataValue::object()};
  }
  stack->data->root.set(namespace_text, document->root);
  player::normalize_item_stack_data(stack.value());
  if (!stack_data_within_limit(stack->data)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "item stack data exceeds size limit");
  }
  if (!store_stack_at_location(*player_state, location, std::move(stack.value()))) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "failed to store item stack");
  }
  return ok(ctx);
}

ArvResult api_clear_stack_namespace_data(
    void* context,
    ArvItemStackLocation location,
    ArvStringView name_space) {
  PluginCallContext* ctx = call_context(context);
  std::string namespace_text;
  if (ArvResult valid = validate_namespace_write(
          ctx,
          name_space,
          "items.clear_stack_namespace_data",
          namespace_text);
      valid.code != ARV_RESULT_OK) {
    return valid;
  }
  player::PlayerState* player_state = require_player(ctx, location);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  if (!location_is_valid(*player_state, location)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "item stack location is invalid");
  }

  std::string error;
  std::optional<player::ItemStack> stack =
      stack_from_location(*player_state, location, error);
  if (!stack.has_value()) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "item stack is empty");
  }
  if (stack->data.has_value()) {
    if (io::DataValue::Object* object = stack->data->root.object_if(); object != nullptr) {
      object->erase(namespace_text);
    }
    player::normalize_item_stack_data(stack.value());
  }
  if (!store_stack_at_location(*player_state, location, std::move(stack.value()))) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "failed to store item stack");
  }
  return ok(ctx);
}

ArvResult api_give_stack(
    void* context,
    ArvPlayerID player,
    ArvStringView item_name,
    uint16_t count,
    ArvStringView name_space,
    ArvDataDocumentHandle document_handle) {
  PluginCallContext* ctx = call_context(context);
  std::string namespace_text;
  if (ArvResult valid = validate_namespace_write(
          ctx,
          name_space,
          "items.give_stack",
          namespace_text);
      valid.code != ARV_RESULT_OK) {
    return valid;
  }
  if (count == 0U) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "item stack count must be greater than zero");
  }
  if (ctx->impl->runtime.item_registry == nullptr) {
    return make_result(ctx, ARV_RESULT_UNSUPPORTED, "item registry is unavailable");
  }
  player::PlayerState* player_state = player_from_id(ctx, player);
  if (player_state == nullptr) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "player not found");
  }
  const io::DataDocument* document = document_from_handle(ctx, document_handle);
  if (document == nullptr) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "item data document is invalid");
  }
  const std::string item_name_text = abi_string(item_name);
  const std::optional<player::ItemID> item_id =
      ctx->impl->runtime.item_registry->find_item(item_name_text);
  if (!item_id.has_value()) {
    return make_result(ctx, ARV_RESULT_NOT_FOUND, "item not found: " + item_name_text);
  }

  player::ItemStack stack{item_id.value(), count};
  stack.data = io::DataDocument{1U, io::DataValue::object()};
  stack.data->root.set(namespace_text, document->root);
  player::normalize_item_stack_data(stack);
  if (!stack_data_within_limit(stack.data)) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "item stack data exceeds size limit");
  }

  const player::InventoryInsertResult result =
      player_state->inventory.insert_stack(std::move(stack), *ctx->impl->runtime.item_registry);
  if (!result.complete()) {
    return make_result(ctx, ARV_RESULT_INVALID_ARGUMENT, "player inventory has no room for item stack");
  }
  return ok(ctx);
}

} // namespace

void fill_items_api(PluginAPIStorage& storage) {
  storage.items_api = {
      &api_item_stack_info,
      &api_load_stack_data,
      &api_load_stack_namespace_data,
      &api_save_stack_namespace_data,
      &api_clear_stack_namespace_data,
      &api_give_stack,
  };
}

} // namespace arv::plugin
