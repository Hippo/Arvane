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
