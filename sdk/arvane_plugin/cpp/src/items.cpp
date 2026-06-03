#include "arv/items.hpp"

namespace arv {
namespace {

[[nodiscard]] ItemStackInfo from_abi(const ArvItemStackInfo& info) noexcept {
  return {
      info.present,
      info.item_id,
      view(info.item_name),
      info.count,
      info.has_data};
}

} // namespace

ItemStackRef::ItemStackRef() noexcept = default;

ItemStackRef::ItemStackRef(const ArvAPI* api, ArvItemStackLocation location) noexcept
    : api_(api), location_(location) {
}

bool ItemStackRef::valid() const noexcept {
  return api_ != nullptr;
}

ArvItemStackLocation ItemStackRef::location() const noexcept {
  return location_;
}

ValueResult<ItemStackInfo> ItemStackRef::info() const {
  ArvItemStackInfo stack{};
  if (!available() || api_->items->stack_info == nullptr) {
    return {Result::unsupported("items API is unavailable"), {}};
  }
  return {
      Result{api_->items->stack_info(api_->items_ctx, location_, &stack)},
      from_abi(stack)};
}

ValueResult<DataDocument> ItemStackRef::load_data() const {
  ArvDataDocumentHandle handle{};
  if (!available() || api_->items->load_stack_data == nullptr) {
    return {Result::unsupported("items API is unavailable"), {}};
  }
  Result result{api_->items->load_stack_data(api_->items_ctx, location_, &handle)};
  return {result, result ? DataDocument{api_, handle} : DataDocument{}};
}

ValueResult<DataDocument> ItemStackRef::load_namespace_data() const {
  const std::string_view name_space = default_namespace();
  if (name_space.empty()) {
    return {Result{ResultCode::InvalidArgument, "plugin has no owned namespace"}, {}};
  }
  return load_namespace_data(name_space);
}

ValueResult<DataDocument> ItemStackRef::load_namespace_data(std::string_view name_space) const {
  ArvDataDocumentHandle handle{};
  if (!available() || api_->items->load_stack_namespace_data == nullptr) {
    return {Result::unsupported("items API is unavailable"), {}};
  }
  Result result{api_->items->load_stack_namespace_data(
      api_->items_ctx,
      location_,
      view(name_space),
      &handle)};
  return {result, result ? DataDocument{api_, handle} : DataDocument{}};
}

Result ItemStackRef::save_namespace_data(const DataDocument& document) const {
  const std::string_view name_space = default_namespace();
  if (name_space.empty()) {
    return Result{ResultCode::InvalidArgument, "plugin has no owned namespace"};
  }
  return save_namespace_data(name_space, document);
}

Result ItemStackRef::save_namespace_data(
    std::string_view name_space,
    const DataDocument& document) const {
  if (!available() || api_->items->save_stack_namespace_data == nullptr) {
    return Result::unsupported("items API is unavailable");
  }
  return Result{api_->items->save_stack_namespace_data(
      api_->items_ctx,
      location_,
      view(name_space),
      document.handle())};
}

Result ItemStackRef::clear_namespace_data() const {
  const std::string_view name_space = default_namespace();
  if (name_space.empty()) {
    return Result{ResultCode::InvalidArgument, "plugin has no owned namespace"};
  }
  return clear_namespace_data(name_space);
}

Result ItemStackRef::clear_namespace_data(std::string_view name_space) const {
  if (!available() || api_->items->clear_stack_namespace_data == nullptr) {
    return Result::unsupported("items API is unavailable");
  }
  return Result{api_->items->clear_stack_namespace_data(
      api_->items_ctx,
      location_,
      view(name_space))};
}

std::string_view ItemStackRef::default_namespace() const {
  if (api_ == nullptr || api_->context == nullptr ||
      api_->context->namespace_count == nullptr ||
      api_->context->namespace_at == nullptr ||
      api_->context->namespace_count(api_->context_ctx) == 0U) {
    return {};
  }
  return view(api_->context->namespace_at(api_->context_ctx, 0U));
}

bool ItemStackRef::available() const noexcept {
  return api_ != nullptr && api_->items != nullptr;
}

Items::Items(const ArvAPI* api) noexcept : api_(api) {
}

ItemStackRef Items::selected_hotbar(ArvPlayerID player) const noexcept {
  return ItemStackRef{
      api_,
      {player, ARV_ITEM_STACK_SELECTED_HOTBAR, 0U}};
}

ItemStackRef Items::inventory_slot(
    ArvPlayerID player,
    std::uint32_t slot_index) const noexcept {
  return ItemStackRef{
      api_,
      {player, ARV_ITEM_STACK_INVENTORY_SLOT, slot_index}};
}

ItemStackRef Items::cursor(ArvPlayerID player) const noexcept {
  return ItemStackRef{
      api_,
      {player, ARV_ITEM_STACK_CURSOR, 0U}};
}

} // namespace arv
