#pragma once

#include "arv/asset/pack.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/time.hpp"
#include "arv/core/types.hpp"
#include "arv/player/item.hpp"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::crafting {

inline constexpr std::string_view SmeltingFuelItemTag = "arvane:smelting/fuel";

struct SmeltingRule {
  std::string input_name{};
  player::ItemID input{};
  u16 input_count{1};
  std::string output_name{};
  player::ItemID output{};
  u16 output_count{1};
  Seconds duration_secs{10.0F};
};

struct SmeltingRegistryError {
  std::string message{};
};

class SmeltingRuleRegistry {
public:
  using Result = Expected<SmeltingRuleRegistry, SmeltingRegistryError>;

  [[nodiscard]] static Result build(std::vector<SmeltingRule> rules);

  [[nodiscard]] const SmeltingRule* find_by_input(player::ItemID input) const noexcept;
  [[nodiscard]] std::span<const SmeltingRule> rules() const noexcept;

private:
  std::vector<SmeltingRule> rules_{};
  HashMap<player::ItemID, usize> rule_by_input_{};
};

[[nodiscard]] Expected<std::vector<SmeltingRule>, SmeltingRegistryError>
parse_smelting_rules_json(
    std::string_view text,
    const player::ItemRegistry& item_registry,
    std::string_view source_name = "smelting data");

[[nodiscard]] SmeltingRuleRegistry::Result load_smelting_rule_registry_from_packs(
    const asset::PackManager& packs,
    const player::ItemRegistry& item_registry);

} // namespace arv::crafting
