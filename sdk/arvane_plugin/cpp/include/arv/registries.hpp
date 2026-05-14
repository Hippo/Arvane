#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

class Registries {
public:
  explicit constexpr Registries(const ArvAPI* api) noexcept : api_(api) {
  }

  [[nodiscard]] Result register_block_behavior(std::string_view id) const {
    return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_block_behavior, id);
  }

  [[nodiscard]] Result register_item_behavior(std::string_view id) const {
    return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_item_behavior, id);
  }

  [[nodiscard]] Result register_entity_goal(std::string_view id) const {
    return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_entity_goal, id);
  }

  [[nodiscard]] Result register_entity_behavior(std::string_view id) const {
    return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_entity_behavior, id);
  }

  [[nodiscard]] Result register_component_codec(std::string_view id) const {
    return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_component_codec, id);
  }

  [[nodiscard]] Result register_loot_function(std::string_view id) const {
    return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_loot_function, id);
  }

  [[nodiscard]] Result register_recipe_type(std::string_view id) const {
    return call(api_ == nullptr || api_->registries == nullptr ? nullptr : api_->registries->register_recipe_type, id);
  }

  [[nodiscard]] ValueResult<std::uint32_t> find_block_variant(
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

  [[nodiscard]] ValueResult<std::string_view> block_variant_name(
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

  [[nodiscard]] ValueResult<std::uint32_t> air_block_variant() const {
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

  [[nodiscard]] bool is_air_block_variant(std::uint32_t block_variant) const {
    return api_ != nullptr && api_->registries != nullptr &&
           api_->registries->is_air_block_variant != nullptr &&
           api_->registries->is_air_block_variant(
               api_->registries_ctx,
               block_variant);
  }

private:
  using RegistryMember = ArvResult (*)(void*, ArvResourceID);

  [[nodiscard]] Result call(RegistryMember member, std::string_view id) const {
    if (api_ == nullptr || api_->registries == nullptr || member == nullptr) {
      return Result::unsupported("registry API is unavailable");
    }
    return Result{member(api_->registries_ctx, view(id))};
  }

  const ArvAPI* api_{};
};

} // namespace arv
