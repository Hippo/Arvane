#include "arv/registries.hpp"

namespace arv {

Registries::Registries(const ArvAPI* api) noexcept : api_(api) {
}

Result Registries::register_block_behavior(std::string_view id) const {
  return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_block_behavior, id);
}

Result Registries::register_item_behavior(std::string_view id) const {
  return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_item_behavior, id);
}

Result Registries::register_entity_goal(std::string_view id) const {
  return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_entity_goal, id);
}

Result Registries::register_entity_behavior(std::string_view id) const {
  return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_entity_behavior, id);
}

Result Registries::register_component_codec(std::string_view id) const {
  return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_component_codec, id);
}

Result Registries::register_loot_function(std::string_view id) const {
  return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_loot_function, id);
}

Result Registries::register_recipe_type(std::string_view id) const {
  return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_recipe_type, id);
}

ValueResult<std::uint32_t> Registries::find_block_variant(
    std::string_view id) const {
  std::uint32_t block_variant{};
  if (api_ == nullptr || api_->registries == nullptr ||
      api_->registries->find_block_variant == nullptr) {
    return {Result::unsupported("registry API is unavailable"), block_variant};
  }
  return {
      Result{api_->registries->find_block_variant(
          api_->registries_ctx,
          view(id),
          &block_variant)},
      block_variant};
}

ValueResult<std::string_view> Registries::block_variant_name(
    std::uint32_t block_variant) const {
  ArvStringView name{};
  if (api_ == nullptr || api_->registries == nullptr ||
      api_->registries->block_variant_name == nullptr) {
    return {Result::unsupported("registry API is unavailable"), {}};
  }
  return {
      Result{api_->registries->block_variant_name(
          api_->registries_ctx,
          block_variant,
          &name)},
      view(name)};
}

ValueResult<std::uint32_t> Registries::air_block_variant() const {
  std::uint32_t block_variant{};
  if (api_ == nullptr || api_->registries == nullptr ||
      api_->registries->air_block_variant == nullptr) {
    return {Result::unsupported("registry API is unavailable"), block_variant};
  }
  return {
      Result{api_->registries->air_block_variant(
          api_->registries_ctx,
          &block_variant)},
      block_variant};
}

bool Registries::is_air_block_variant(std::uint32_t block_variant) const {
  return api_ != nullptr && api_->registries != nullptr &&
         api_->registries->is_air_block_variant != nullptr &&
         api_->registries->is_air_block_variant(
             api_->registries_ctx,
             block_variant);
}

Result Registries::call(RegistryMember member, std::string_view id) const {
  if (api_ == nullptr || api_->registries == nullptr || member == nullptr) {
    return Result::unsupported("registry API is unavailable");
  }
  return Result{member(api_->registries_ctx, view(id))};
}

} // namespace arv
