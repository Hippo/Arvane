#pragma once

#include "arv/api.h"
#include "arv/result.hpp"
#include "arv/types.hpp"

#include <string_view>

namespace arv {

class Registries {
public:
  explicit Registries(const ArvAPI* api) noexcept;

  [[nodiscard]] Result register_block_behavior(std::string_view id) const;

  [[nodiscard]] Result register_item_behavior(std::string_view id) const;

  [[nodiscard]] Result register_entity_goal(std::string_view id) const;

  [[nodiscard]] Result register_entity_behavior(std::string_view id) const;

  [[nodiscard]] Result register_component_codec(std::string_view id) const;

  [[nodiscard]] Result register_loot_function(std::string_view id) const;

  [[nodiscard]] Result register_recipe_type(std::string_view id) const;

  [[nodiscard]] ValueResult<std::uint32_t> find_block_variant(
      std::string_view id) const;

  [[nodiscard]] ValueResult<std::string_view> block_variant_name(
      std::uint32_t block_variant) const;

  [[nodiscard]] ValueResult<std::uint32_t> air_block_variant() const;

  [[nodiscard]] bool is_air_block_variant(std::uint32_t block_variant) const;

private:
  using RegistryMember = ArvResult (*)(void*, ArvResourceID);

  [[nodiscard]] Result call(RegistryMember member, std::string_view id) const;

  const ArvAPI* api_{};
};

} // namespace arv
