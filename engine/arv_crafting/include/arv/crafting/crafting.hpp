#pragma once

#include "arv/asset/pack.hpp"
#include "arv/core/expected.hpp"
#include "arv/core/types.hpp"
#include "arv/player/inventory.hpp"

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::crafting {

inline constexpr std::string_view CraftingRecipeType = "arvane:crafting";
inline constexpr std::string_view MiscCraftingCategoryID = "arvane:misc";

enum class CraftingIngredientKind : u8 {
  Item,
  ItemTag,
};

struct CraftingIcon {
  std::string item_name{};
  player::ItemID item{};
};

struct CraftingIngredient {
  CraftingIngredientKind kind{CraftingIngredientKind::Item};
  std::string item_name{};
  std::string tag_name{};
  player::ItemID item{};
  std::vector<player::ItemID> tag_items{};
  u16 count{};
};

struct CraftingResult {
  std::string item_name{};
  player::ItemID item{};
  u16 count{};
};

struct CraftingRecipeUnlock {
  bool default_unlocked{};
  std::vector<std::string> collect_item_names{};
  std::vector<player::ItemID> collect_items{};
  std::string blueprint_item_name{};
  std::optional<player::ItemID> blueprint_item{};
};

struct CraftingRecipe {
  std::string id{};
  std::string type{std::string{CraftingRecipeType}};
  std::string category{std::string{MiscCraftingCategoryID}};
  CraftingIcon icon{};
  std::vector<CraftingIngredient> ingredients{};
  CraftingResult result{};
  CraftingRecipeUnlock unlock{};
  std::vector<std::string> search_terms{};
};

struct CraftingCategory {
  std::string id{};
  std::string display_name{};
  CraftingIcon icon{};
  i32 sort_order{};
};

struct CraftingLoadDiagnostic {
  std::string pack_id{};
  std::string pack_version{};
  std::filesystem::path file{};
  std::string resource_id{};
  std::string message{};

  [[nodiscard]] std::string describe() const;
};

struct CraftingRegistryError {
  std::string message{};
};

class CraftingRecipeRegistry {
public:
  [[nodiscard]] const CraftingCategory* category(std::string_view id) const noexcept;
  [[nodiscard]] const CraftingRecipe* recipe(std::string_view id) const noexcept;
  [[nodiscard]] bool has_category(std::string_view id) const noexcept;
  [[nodiscard]] bool has_recipe(std::string_view id) const noexcept;
  [[nodiscard]] std::span<const CraftingCategory> categories() const noexcept;
  [[nodiscard]] std::span<const CraftingRecipe> recipes() const noexcept;
  [[nodiscard]] std::vector<const CraftingRecipe*> recipes_in_category(std::string_view id) const;

private:
  friend struct CraftingRegistryBuilder;

  std::vector<CraftingCategory> categories_{};
  std::vector<CraftingRecipe> recipes_{};
  HashMap<std::string, usize> category_by_id_{};
  HashMap<std::string, usize> recipe_by_id_{};
};

struct CraftingRegistryLoadOutput {
  CraftingRecipeRegistry registry{};
  std::vector<CraftingLoadDiagnostic> diagnostics{};
};

using CraftingRegistryLoadResult =
    Expected<CraftingRegistryLoadOutput, CraftingRegistryError>;

[[nodiscard]] CraftingRegistryLoadResult load_crafting_registry_from_packs(
    const asset::PackManager& packs,
    const player::ItemRegistry& item_registry);

[[nodiscard]] bool item_has_tag(
    const player::ItemRegistry& item_registry,
    player::ItemID item,
    std::string_view tag) noexcept;
[[nodiscard]] bool item_tag_exists(
    const player::ItemRegistry& item_registry,
    std::string_view tag) noexcept;
[[nodiscard]] std::vector<player::ItemID> items_with_tag(
    const player::ItemRegistry& item_registry,
    std::string_view tag);

enum class CraftingFailureReason : u8 {
  None,
  UnknownRecipe,
  RecipeNotUnlocked,
  MissingIngredients,
  InventoryFull,
  InvalidRequest,
  StationRequired,
};

struct CraftingIngredientAvailability {
  std::string label{};
  u16 required{};
  u32 owned{};
  bool satisfied{};
};

struct CraftingAttemptResult {
  CraftingFailureReason reason{CraftingFailureReason::None};
  std::vector<CraftingIngredientAvailability> ingredients{};

  [[nodiscard]] bool success() const noexcept {
    return reason == CraftingFailureReason::None;
  }
};

[[nodiscard]] bool recipe_is_unlocked(
    std::span<const std::string> unlocked_recipe_ids,
    std::string_view recipe_id) noexcept;
void add_default_recipe_unlocks(
    std::vector<std::string>& unlocked_recipe_ids,
    const CraftingRecipeRegistry& registry);
void prune_missing_recipe_unlocks(
    std::vector<std::string>& unlocked_recipe_ids,
    const CraftingRecipeRegistry& registry);
[[nodiscard]] std::vector<std::string> normalized_recipe_unlocks(
    std::span<const std::string> unlocked_recipe_ids,
    const CraftingRecipeRegistry& registry);
[[nodiscard]] std::vector<std::string> unlock_recipes_for_collected_item(
    std::vector<std::string>& unlocked_recipe_ids,
    player::ItemID item,
    const CraftingRecipeRegistry& registry);

[[nodiscard]] u32 count_matching_ingredient(
    const player::Inventory& inventory,
    const CraftingIngredient& ingredient);
[[nodiscard]] std::vector<CraftingIngredientAvailability> ingredient_availability(
    const player::Inventory& inventory,
    const CraftingRecipe& recipe,
    const player::ItemRegistry& item_registry);
[[nodiscard]] CraftingAttemptResult craft_recipe_once(
    player::Inventory& inventory,
    const CraftingRecipe& recipe,
    const player::ItemRegistry& item_registry);

[[nodiscard]] std::string crafting_failure_reason_name(CraftingFailureReason reason);

} // namespace arv::crafting
