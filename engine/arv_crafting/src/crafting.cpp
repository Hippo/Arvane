#include "arv/crafting/crafting.hpp"

#include "arv/asset/pack.hpp"
#include "arv/io/data_codecs.hpp"
#include "arv/io/files.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace arv::crafting {
struct CraftingRegistryBuilder {
  CraftingRecipeRegistry registry;
  std::vector<CraftingLoadDiagnostic> diagnostics;

  void add_category(CraftingCategory category, const asset::PackDataFile& file);
  void add_recipe(CraftingRecipe recipe, const asset::PackDataFile& file);
  void ensure_misc_category(const player::ItemRegistry& item_registry);
  void validate_recipe_categories();
  void sort_and_reindex();
};

namespace {

[[nodiscard]] const io::DataValue* member(
    const io::DataValue& object,
    std::string_view key) noexcept {
  return object.get(key);
}

[[nodiscard]] bool string_member(
    const io::DataValue& object,
    std::string_view key,
    std::string& out) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr || value->kind() != io::DataKind::String) {
    return false;
  }
  out = std::string{value->as_string_or({})};
  return true;
}

[[nodiscard]] bool optional_string_member(
    const io::DataValue& object,
    std::string_view key,
    std::string& out) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr) {
    return true;
  }
  if (value->kind() != io::DataKind::String) {
    return false;
  }
  out = std::string{value->as_string_or({})};
  return true;
}

[[nodiscard]] bool optional_i32_member(
    const io::DataValue& object,
    std::string_view key,
    i32& out) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr) {
    return true;
  }
  if (!io::is_integer_value(*value)) {
    return false;
  }
  out = static_cast<i32>(value->as_i64_or(static_cast<i64>(out)));
  return true;
}

[[nodiscard]] std::optional<u16> positive_u16_member(
    const io::DataValue& object,
    std::string_view key) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr || !io::is_integer_value(*value)) {
    return std::nullopt;
  }
  const u64 count = value->as_u64_or(0U);
  if (count == 0U || count > 65535U) {
    return std::nullopt;
  }
  return static_cast<u16>(count);
}

[[nodiscard]] CraftingLoadDiagnostic diagnostic(
    const asset::PackDataFile& file,
    std::string message,
    std::string resource_id = {}) {
  return {
      file.pack_id,
      file.pack_version,
      file.relative_path,
      std::move(resource_id),
      std::move(message)};
}

[[nodiscard]] std::string id_error(std::string_view field, std::string_view value) {
  if (value.empty()) {
    return std::string{field} + " must be non-empty";
  }
  auto parsed = asset::ResourceID::parse(value);
  if (!parsed) {
    return std::string{field} + " is invalid: " + parsed.error();
  }
  return {};
}

[[nodiscard]] std::optional<player::ItemID> resolve_real_item(
    const player::ItemRegistry& registry,
    std::string_view item_name) {
  const std::optional<player::ItemID> item_id = registry.find_item(item_name);
  if (!item_id.has_value() || registry.is_air(item_id.value()) ||
      item_name == player::MissingItemName) {
    return std::nullopt;
  }
  const player::ItemRecord* item = registry.item(item_id.value());
  return item == nullptr ? std::nullopt : item_id;
}

[[nodiscard]] bool read_icon(
    const io::DataValue& object,
    const player::ItemRegistry& item_registry,
    CraftingIcon& icon,
    std::string& error,
    bool required) {
  const io::DataValue* icon_value = member(object, "icon");
  if (icon_value == nullptr) {
    if (required) {
      error = "icon is required";
      return false;
    }
    return true;
  }
  if (icon_value->kind() != io::DataKind::Object) {
    error = "icon must be an object";
    return false;
  }
  std::string item_name;
  if (!string_member(*icon_value, "item", item_name)) {
    error = "icon.item must be a string";
    return false;
  }
  const std::optional<player::ItemID> item = resolve_real_item(item_registry, item_name);
  if (!item.has_value()) {
    error = "icon.item references an unknown item: " + item_name;
    return false;
  }
  icon.item_name = std::move(item_name);
  icon.item = item.value();
  return true;
}

[[nodiscard]] bool read_string_array(
    const io::DataValue& object,
    std::string_view key,
    std::vector<std::string>& out,
    std::string& error) {
  const io::DataValue* value = member(object, key);
  if (value == nullptr) {
    return true;
  }
  const io::DataValue::Array* array = value->array_if();
  if (array == nullptr) {
    error = std::string{key} + " must be an array";
    return false;
  }
  for (const io::DataValue& entry : *array) {
    if (entry.kind() != io::DataKind::String) {
      error = std::string{key} + " entries must be strings";
      return false;
    }
    std::string text{entry.as_string_or({})};
    if (text.empty()) {
      error = std::string{key} + " entries must be non-empty";
      return false;
    }
    out.push_back(std::move(text));
  }
  return true;
}

[[nodiscard]] bool read_item_array(
    const io::DataValue& object,
    std::string_view key,
    const player::ItemRegistry& item_registry,
    std::vector<std::string>& names,
    std::vector<player::ItemID>& ids,
    std::string& error) {
  const usize before_names = names.size();
  if (!read_string_array(object, key, names, error)) {
    return false;
  }
  for (usize index = before_names; index < names.size(); ++index) {
    const std::optional<player::ItemID> item =
        resolve_real_item(item_registry, names[index]);
    if (!item.has_value()) {
      error = std::string{key} + " references an unknown item: " + names[index];
      return false;
    }
    ids.push_back(item.value());
  }
  return true;
}

[[nodiscard]] bool parse_category_entry(
    const io::DataValue& entry,
    const asset::PackDataFile& file,
    const player::ItemRegistry& item_registry,
    CraftingCategory& category,
    CraftingLoadDiagnostic& out_error) {
  if (entry.kind() != io::DataKind::Object) {
    out_error = diagnostic(file, "crafting category entry must be an object");
    return false;
  }

  if (!string_member(entry, "id", category.id)) {
    out_error = diagnostic(file, "crafting category is missing string field id");
    return false;
  }
  if (std::string error = id_error("crafting category id", category.id); !error.empty()) {
    out_error = diagnostic(file, std::move(error), category.id);
    return false;
  }
  if (!string_member(entry, "name", category.display_name) || category.display_name.empty()) {
    out_error = diagnostic(file, "crafting category requires non-empty string field name", category.id);
    return false;
  }
  std::string error;
  if (!read_icon(entry, item_registry, category.icon, error, true)) {
    out_error = diagnostic(file, std::move(error), category.id);
    return false;
  }
  if (!optional_i32_member(entry, "sort_order", category.sort_order)) {
    out_error = diagnostic(file, "crafting category sort_order must be an integer", category.id);
    return false;
  }
  return true;
}

[[nodiscard]] bool parse_ingredient_entry(
    const io::DataValue& entry,
    const asset::PackDataFile& file,
    const player::ItemRegistry& item_registry,
    CraftingIngredient& ingredient,
    CraftingLoadDiagnostic& out_error,
    std::string_view recipe_id) {
  if (entry.kind() != io::DataKind::Object) {
    out_error = diagnostic(file, "recipe ingredient entry must be an object", std::string{recipe_id});
    return false;
  }
  const io::DataValue* item_value = member(entry, "item");
  const io::DataValue* tag_value = member(entry, "tag");
  if ((item_value == nullptr) == (tag_value == nullptr)) {
    out_error = diagnostic(file, "recipe ingredient requires exactly one of item or tag", std::string{recipe_id});
    return false;
  }

  const std::optional<u16> count = positive_u16_member(entry, "count");
  if (!count.has_value()) {
    out_error = diagnostic(file, "recipe ingredient count must be a positive integer", std::string{recipe_id});
    return false;
  }
  ingredient.count = count.value();

  if (item_value != nullptr) {
    if (item_value->kind() != io::DataKind::String) {
      out_error = diagnostic(file, "recipe ingredient item must be a string", std::string{recipe_id});
      return false;
    }
    ingredient.item_name = std::string{item_value->as_string_or({})};
    const std::optional<player::ItemID> item =
        resolve_real_item(item_registry, ingredient.item_name);
    if (!item.has_value()) {
      out_error = diagnostic(
          file,
          "recipe ingredient item references an unknown item: " + ingredient.item_name,
          std::string{recipe_id});
      return false;
    }
    ingredient.kind = CraftingIngredientKind::Item;
    ingredient.item = item.value();
    return true;
  }

  if (tag_value->kind() != io::DataKind::String) {
    out_error = diagnostic(file, "recipe ingredient tag must be a string", std::string{recipe_id});
    return false;
  }
  ingredient.tag_name = std::string{tag_value->as_string_or({})};
  if (std::string error = id_error("recipe ingredient tag", ingredient.tag_name); !error.empty()) {
    out_error = diagnostic(file, std::move(error), std::string{recipe_id});
    return false;
  }
  ingredient.tag_items = items_with_tag(item_registry, ingredient.tag_name);
  if (ingredient.tag_items.empty()) {
    out_error = diagnostic(
        file,
        "recipe ingredient tag has no matching items: " + ingredient.tag_name,
        std::string{recipe_id});
    return false;
  }
  ingredient.kind = CraftingIngredientKind::ItemTag;
  return true;
}

[[nodiscard]] bool parse_result(
    const io::DataValue& entry,
    const asset::PackDataFile& file,
    const player::ItemRegistry& item_registry,
    CraftingResult& result,
    CraftingLoadDiagnostic& out_error,
    std::string_view recipe_id) {
  const io::DataValue* result_value = member(entry, "result");
  if (result_value == nullptr || result_value->kind() != io::DataKind::Object) {
    out_error = diagnostic(file, "recipe result must be an object", std::string{recipe_id});
    return false;
  }
  if (!string_member(*result_value, "item", result.item_name)) {
    out_error = diagnostic(file, "recipe result.item must be a string", std::string{recipe_id});
    return false;
  }
  const std::optional<player::ItemID> item =
      resolve_real_item(item_registry, result.item_name);
  if (!item.has_value()) {
    out_error = diagnostic(
        file,
        "recipe result.item references an unknown item: " + result.item_name,
        std::string{recipe_id});
    return false;
  }
  const std::optional<u16> count = positive_u16_member(*result_value, "count");
  if (!count.has_value()) {
    out_error = diagnostic(file, "recipe result.count must be a positive integer", std::string{recipe_id});
    return false;
  }
  const player::ItemRecord* record = item_registry.item(item.value());
  if (record == nullptr || count.value() > record->max_stack_size) {
    out_error = diagnostic(file, "recipe result.count exceeds the item stack limit", std::string{recipe_id});
    return false;
  }
  result.item = item.value();
  result.count = count.value();
  return true;
}

[[nodiscard]] bool parse_unlock(
    const io::DataValue& entry,
    const asset::PackDataFile& file,
    const player::ItemRegistry& item_registry,
    CraftingRecipeUnlock& unlock,
    CraftingLoadDiagnostic& out_error,
    std::string_view recipe_id) {
  const io::DataValue* unlock_value = member(entry, "unlock");
  if (unlock_value == nullptr) {
    return true;
  }
  if (unlock_value->kind() != io::DataKind::Object) {
    out_error = diagnostic(file, "recipe unlock must be an object", std::string{recipe_id});
    return false;
  }
  if (const io::DataValue* default_value = member(*unlock_value, "default");
      default_value != nullptr) {
    if (default_value->kind() != io::DataKind::Bool) {
      out_error = diagnostic(file, "recipe unlock.default must be a boolean", std::string{recipe_id});
      return false;
    }
    unlock.default_unlocked = default_value->as_bool_or(false);
  }

  std::string error;
  if (!read_item_array(
          *unlock_value,
          "collect_items",
          item_registry,
          unlock.collect_item_names,
          unlock.collect_items,
          error)) {
    out_error = diagnostic(file, std::move(error), std::string{recipe_id});
    return false;
  }
  if (!optional_string_member(*unlock_value, "blueprint", unlock.blueprint_item_name)) {
    out_error = diagnostic(file, "recipe unlock.blueprint must be a string", std::string{recipe_id});
    return false;
  }
  if (!unlock.blueprint_item_name.empty()) {
    const std::optional<player::ItemID> item =
        resolve_real_item(item_registry, unlock.blueprint_item_name);
    if (!item.has_value()) {
      out_error = diagnostic(
          file,
          "recipe unlock.blueprint references an unknown item: " + unlock.blueprint_item_name,
          std::string{recipe_id});
      return false;
    }
    unlock.blueprint_item = item.value();
  }
  return true;
}

[[nodiscard]] bool parse_recipe_entry(
    const io::DataValue& entry,
    const asset::PackDataFile& file,
    const player::ItemRegistry& item_registry,
    CraftingRecipe& recipe,
    CraftingLoadDiagnostic& out_error) {
  if (entry.kind() != io::DataKind::Object) {
    out_error = diagnostic(file, "crafting recipe entry must be an object");
    return false;
  }
  if (!string_member(entry, "id", recipe.id)) {
    out_error = diagnostic(file, "crafting recipe is missing string field id");
    return false;
  }
  if (std::string error = id_error("crafting recipe id", recipe.id); !error.empty()) {
    out_error = diagnostic(file, std::move(error), recipe.id);
    return false;
  }
  if (!optional_string_member(entry, "type", recipe.type) || recipe.type.empty()) {
    out_error = diagnostic(file, "crafting recipe type must be a non-empty string", recipe.id);
    return false;
  }
  if (recipe.type != CraftingRecipeType) {
    out_error = diagnostic(file, "unsupported crafting recipe type: " + recipe.type, recipe.id);
    return false;
  }
  if (!optional_string_member(entry, "category", recipe.category) || recipe.category.empty()) {
    out_error = diagnostic(file, "crafting recipe category must be a non-empty string", recipe.id);
    return false;
  }
  if (std::string error = id_error("crafting recipe category", recipe.category); !error.empty()) {
    out_error = diagnostic(file, std::move(error), recipe.id);
    return false;
  }

  if (!parse_result(entry, file, item_registry, recipe.result, out_error, recipe.id)) {
    return false;
  }
  std::string error;
  if (!read_icon(entry, item_registry, recipe.icon, error, false)) {
    out_error = diagnostic(file, std::move(error), recipe.id);
    return false;
  }
  if (recipe.icon.item_name.empty()) {
    recipe.icon.item_name = recipe.result.item_name;
    recipe.icon.item = recipe.result.item;
  }

  const io::DataValue* ingredients_value = member(entry, "ingredients");
  const io::DataValue::Array* ingredients =
      ingredients_value == nullptr ? nullptr : ingredients_value->array_if();
  if (ingredients == nullptr || ingredients->empty()) {
    out_error = diagnostic(file, "recipe ingredients must be a non-empty array", recipe.id);
    return false;
  }
  for (const io::DataValue& ingredient_entry : *ingredients) {
    CraftingIngredient ingredient;
    if (!parse_ingredient_entry(
            ingredient_entry,
            file,
            item_registry,
            ingredient,
            out_error,
            recipe.id)) {
      return false;
    }
    recipe.ingredients.push_back(std::move(ingredient));
  }
  if (!parse_unlock(entry, file, item_registry, recipe.unlock, out_error, recipe.id)) {
    return false;
  }
  if (!read_string_array(entry, "search_terms", recipe.search_terms, error)) {
    out_error = diagnostic(file, std::move(error), recipe.id);
    return false;
  }
  return true;
}

[[nodiscard]] Expected<io::DataValue, CraftingLoadDiagnostic> read_json_root(
    const asset::PackDataFile& file) {
  auto text = io::read_text_file_contents(file.path);
  if (!text) {
    return Expected<io::DataValue, CraftingLoadDiagnostic>::failure(
        diagnostic(file, "failed to read file: " + text.error().message));
  }
  auto decoded = io::decode_json_value(text.value());
  if (!decoded) {
    return Expected<io::DataValue, CraftingLoadDiagnostic>::failure(
        diagnostic(file, "failed to parse JSON: " + decoded.error().describe()));
  }
  return Expected<io::DataValue, CraftingLoadDiagnostic>::success(
      std::move(decoded.value().root));
}

template<typename ParseFn, typename AppendFn>
void parse_entries_from_file(
    const asset::PackDataFile& file,
    std::string_view array_key,
    ParseFn parse_entry,
    AppendFn append_entry,
    std::vector<CraftingLoadDiagnostic>& diagnostics) {
  auto root = read_json_root(file);
  if (!root) {
    diagnostics.push_back(root.error());
    return;
  }

  const io::DataValue* entries = &root.value();
  if (root.value().kind() == io::DataKind::Object) {
    if (const io::DataValue* array = member(root.value(), array_key);
        array != nullptr) {
      entries = array;
    }
  }

  if (const io::DataValue::Array* array = entries->array_if();
      array != nullptr) {
    for (const io::DataValue& entry : *array) {
      parse_entry(entry, append_entry);
    }
    return;
  }
  parse_entry(*entries, append_entry);
}

[[nodiscard]] bool ingredient_matches_stack(
    const CraftingIngredient& ingredient,
    const player::ItemStack& stack) noexcept {
  if (ingredient.kind == CraftingIngredientKind::Item) {
    return stack.item == ingredient.item;
  }
  return std::find(
      ingredient.tag_items.begin(),
      ingredient.tag_items.end(),
      stack.item) != ingredient.tag_items.end();
}

[[nodiscard]] bool consume_ingredient(
    player::Inventory& inventory,
    const CraftingIngredient& ingredient) {
  u16 remaining = ingredient.count;
  for (usize slot = 0; slot < inventory.slot_count() && remaining > 0U; ++slot) {
    const std::optional<player::ItemStack>& stack = inventory.slot(slot);
    if (!stack.has_value() || !ingredient_matches_stack(ingredient, stack.value())) {
      continue;
    }
    const u16 removed = std::min(remaining, stack->count);
    (void)inventory.remove_amount(slot, removed);
    remaining = static_cast<u16>(remaining - removed);
  }
  return remaining == 0U;
}

void sort_unique_unlocks(std::vector<std::string>& values) {
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}

} // namespace

void CraftingRegistryBuilder::add_category(
    CraftingCategory category,
    const asset::PackDataFile& file) {
  if (registry.category_by_id_.contains(category.id)) {
    diagnostics.push_back(diagnostic(
        file,
        "duplicate crafting category id: " + category.id,
        category.id));
    return;
  }
  registry.category_by_id_.emplace(category.id, registry.categories_.size());
  registry.categories_.push_back(std::move(category));
}

void CraftingRegistryBuilder::add_recipe(
    CraftingRecipe recipe,
    const asset::PackDataFile& file) {
  if (registry.recipe_by_id_.contains(recipe.id)) {
    diagnostics.push_back(diagnostic(
        file,
        "duplicate crafting recipe id: " + recipe.id,
        recipe.id));
    return;
  }
  registry.recipe_by_id_.emplace(recipe.id, registry.recipes_.size());
  registry.recipes_.push_back(std::move(recipe));
}

void CraftingRegistryBuilder::ensure_misc_category(
    const player::ItemRegistry& item_registry) {
  if (registry.category_by_id_.contains(std::string{MiscCraftingCategoryID})) {
    return;
  }
  CraftingCategory misc;
  misc.id = std::string{MiscCraftingCategoryID};
  misc.display_name = "Misc";
  misc.sort_order = 10000;
  if (const player::ItemRecord* missing = item_registry.missing_item();
      missing != nullptr) {
    misc.icon.item_name = missing->name;
    misc.icon.item = missing->id;
  }
  registry.category_by_id_.emplace(misc.id, registry.categories_.size());
  registry.categories_.push_back(std::move(misc));
}

void CraftingRegistryBuilder::validate_recipe_categories() {
  for (CraftingRecipe& recipe : registry.recipes_) {
    if (registry.category_by_id_.contains(recipe.category)) {
      continue;
    }
    diagnostics.push_back({
        "",
        "",
        {},
        recipe.id,
        "recipe category does not exist; falling back to " +
            std::string{MiscCraftingCategoryID}});
    recipe.category = std::string{MiscCraftingCategoryID};
  }
}

void CraftingRegistryBuilder::sort_and_reindex() {
  std::sort(
      registry.categories_.begin(),
      registry.categories_.end(),
      [](const CraftingCategory& lhs, const CraftingCategory& rhs) {
        if (lhs.sort_order != rhs.sort_order) {
          return lhs.sort_order < rhs.sort_order;
        }
        if (lhs.display_name != rhs.display_name) {
          return lhs.display_name < rhs.display_name;
        }
        return lhs.id < rhs.id;
      });
  registry.category_by_id_.clear();
  for (usize index = 0; index < registry.categories_.size(); ++index) {
    registry.category_by_id_.emplace(registry.categories_[index].id, index);
  }

  std::sort(
      registry.recipes_.begin(),
      registry.recipes_.end(),
      [](const CraftingRecipe& lhs, const CraftingRecipe& rhs) {
        return lhs.id < rhs.id;
      });
  registry.recipe_by_id_.clear();
  for (usize index = 0; index < registry.recipes_.size(); ++index) {
    registry.recipe_by_id_.emplace(registry.recipes_[index].id, index);
  }
}

std::string CraftingLoadDiagnostic::describe() const {
  std::ostringstream out;
  out << "crafting data";
  if (!pack_id.empty()) {
    out << " pack=" << pack_id;
  }
  if (!pack_version.empty()) {
    out << " version=" << pack_version;
  }
  if (!file.empty()) {
    out << " file=" << file.generic_string();
  }
  if (!resource_id.empty()) {
    out << " resource=" << resource_id;
  }
  out << ": " << message;
  return out.str();
}

const CraftingCategory* CraftingRecipeRegistry::category(std::string_view id) const noexcept {
  const auto found = category_by_id_.find(std::string{id});
  return found == category_by_id_.end() ? nullptr : &categories_[found->second];
}

const CraftingRecipe* CraftingRecipeRegistry::recipe(std::string_view id) const noexcept {
  const auto found = recipe_by_id_.find(std::string{id});
  return found == recipe_by_id_.end() ? nullptr : &recipes_[found->second];
}

bool CraftingRecipeRegistry::has_category(std::string_view id) const noexcept {
  return category(id) != nullptr;
}

bool CraftingRecipeRegistry::has_recipe(std::string_view id) const noexcept {
  return recipe(id) != nullptr;
}

std::span<const CraftingCategory> CraftingRecipeRegistry::categories() const noexcept {
  return categories_;
}

std::span<const CraftingRecipe> CraftingRecipeRegistry::recipes() const noexcept {
  return recipes_;
}

std::vector<const CraftingRecipe*> CraftingRecipeRegistry::recipes_in_category(
    std::string_view id) const {
  std::vector<const CraftingRecipe*> out;
  for (const CraftingRecipe& candidate : recipes_) {
    if (candidate.category == id) {
      out.push_back(&candidate);
    }
  }
  return out;
}

CraftingRegistryLoadResult load_crafting_registry_from_packs(
    const asset::PackManager& packs,
    const player::ItemRegistry& item_registry) {
  CraftingRegistryBuilder builder;

  for (const asset::PackDataFile& file : packs.data_files("crafting_categories", ".json")) {
    parse_entries_from_file(
        file,
        "categories",
        [&](const io::DataValue& entry, const auto& append) {
          CraftingCategory category;
          CraftingLoadDiagnostic error;
          if (!parse_category_entry(entry, file, item_registry, category, error)) {
            builder.diagnostics.push_back(std::move(error));
            return;
          }
          append(std::move(category));
        },
        [&](CraftingCategory category) {
          builder.add_category(std::move(category), file);
        },
        builder.diagnostics);
  }

  builder.ensure_misc_category(item_registry);

  for (const asset::PackDataFile& file : packs.data_files("recipes", ".json")) {
    parse_entries_from_file(
        file,
        "recipes",
        [&](const io::DataValue& entry, const auto& append) {
          CraftingRecipe recipe;
          CraftingLoadDiagnostic error;
          if (!parse_recipe_entry(entry, file, item_registry, recipe, error)) {
            builder.diagnostics.push_back(std::move(error));
            return;
          }
          append(std::move(recipe));
        },
        [&](CraftingRecipe recipe) {
          builder.add_recipe(std::move(recipe), file);
        },
        builder.diagnostics);
  }

  builder.validate_recipe_categories();
  builder.sort_and_reindex();
  return CraftingRegistryLoadResult::success(
      {std::move(builder.registry), std::move(builder.diagnostics)});
}

bool item_has_tag(
    const player::ItemRegistry& item_registry,
    player::ItemID item,
    std::string_view tag) noexcept {
  const player::ItemRecord* record = item_registry.item(item);
  if (record == nullptr) {
    return false;
  }
  return std::find_if(
             record->tags.begin(),
             record->tags.end(),
             [&](const std::string& value) {
               return value == tag;
             }) != record->tags.end();
}

bool item_tag_exists(
    const player::ItemRegistry& item_registry,
    std::string_view tag) noexcept {
  for (u32 index = 0; index < item_registry.item_count(); ++index) {
    if (item_has_tag(item_registry, player::ItemID{index}, tag)) {
      return true;
    }
  }
  return false;
}

std::vector<player::ItemID> items_with_tag(
    const player::ItemRegistry& item_registry,
    std::string_view tag) {
  std::vector<player::ItemID> out;
  for (u32 index = 0; index < item_registry.item_count(); ++index) {
    const player::ItemID item{index};
    if (item_has_tag(item_registry, item, tag)) {
      out.push_back(item);
    }
  }
  return out;
}

bool recipe_is_unlocked(
    std::span<const std::string> unlocked_recipe_ids,
    std::string_view recipe_id) noexcept {
  return std::find(unlocked_recipe_ids.begin(), unlocked_recipe_ids.end(), recipe_id) !=
         unlocked_recipe_ids.end();
}

void add_default_recipe_unlocks(
    std::vector<std::string>& unlocked_recipe_ids,
    const CraftingRecipeRegistry& registry) {
  for (const CraftingRecipe& recipe : registry.recipes()) {
    if (recipe.unlock.default_unlocked) {
      unlocked_recipe_ids.push_back(recipe.id);
    }
  }
  sort_unique_unlocks(unlocked_recipe_ids);
}

void prune_missing_recipe_unlocks(
    std::vector<std::string>& unlocked_recipe_ids,
    const CraftingRecipeRegistry& registry) {
  std::vector<std::string> kept;
  kept.reserve(unlocked_recipe_ids.size());
  for (const std::string& id : unlocked_recipe_ids) {
    if (registry.has_recipe(id)) {
      kept.push_back(id);
    }
  }
  sort_unique_unlocks(kept);
  unlocked_recipe_ids = std::move(kept);
}

std::vector<std::string> normalized_recipe_unlocks(
    std::span<const std::string> unlocked_recipe_ids,
    const CraftingRecipeRegistry& registry) {
  std::vector<std::string> out{unlocked_recipe_ids.begin(), unlocked_recipe_ids.end()};
  prune_missing_recipe_unlocks(out, registry);
  add_default_recipe_unlocks(out, registry);
  return out;
}

std::vector<std::string> unlock_recipes_for_collected_item(
    std::vector<std::string>& unlocked_recipe_ids,
    player::ItemID item,
    const CraftingRecipeRegistry& registry) {
  std::vector<std::string> unlocked_now;
  add_default_recipe_unlocks(unlocked_recipe_ids, registry);
  for (const CraftingRecipe& recipe : registry.recipes()) {
    if (recipe_is_unlocked(unlocked_recipe_ids, recipe.id)) {
      continue;
    }
    const bool matches_collect_item =
        std::find(recipe.unlock.collect_items.begin(), recipe.unlock.collect_items.end(), item) !=
        recipe.unlock.collect_items.end();
    const bool matches_blueprint_item =
        recipe.unlock.blueprint_item.has_value() && recipe.unlock.blueprint_item.value() == item;
    if (!matches_collect_item && !matches_blueprint_item) {
      continue;
    }
    unlocked_recipe_ids.push_back(recipe.id);
    unlocked_now.push_back(recipe.id);
  }
  sort_unique_unlocks(unlocked_recipe_ids);
  sort_unique_unlocks(unlocked_now);
  return unlocked_now;
}

u32 count_matching_ingredient(
    const player::Inventory& inventory,
    const CraftingIngredient& ingredient) {
  u32 total = 0U;
  for (usize slot = 0; slot < inventory.slot_count(); ++slot) {
    const std::optional<player::ItemStack>& stack = inventory.slot(slot);
    if (stack.has_value() && ingredient_matches_stack(ingredient, stack.value())) {
      total += stack->count;
    }
  }
  return total;
}

std::vector<CraftingIngredientAvailability> ingredient_availability(
    const player::Inventory& inventory,
    const CraftingRecipe& recipe,
    const player::ItemRegistry& item_registry) {
  std::vector<CraftingIngredientAvailability> out;
  out.reserve(recipe.ingredients.size());
  for (const CraftingIngredient& ingredient : recipe.ingredients) {
    const u32 owned = count_matching_ingredient(inventory, ingredient);
    std::string label;
    if (ingredient.kind == CraftingIngredientKind::Item) {
      const player::ItemRecord* item = item_registry.item(ingredient.item);
      label = item == nullptr || item->display_name.empty()
                  ? ingredient.item_name
                  : item->display_name;
    } else {
      label = ingredient.tag_name;
    }
    out.push_back({
        std::move(label),
        ingredient.count,
        owned,
        owned >= ingredient.count});
  }
  return out;
}

CraftingAttemptResult craft_recipe_once(
    player::Inventory& inventory,
    const CraftingRecipe& recipe,
    const player::ItemRegistry& item_registry) {
  CraftingAttemptResult result;
  result.ingredients = ingredient_availability(inventory, recipe, item_registry);

  player::Inventory next = inventory;
  for (const CraftingIngredient& ingredient : recipe.ingredients) {
    if (!consume_ingredient(next, ingredient)) {
      result.reason = CraftingFailureReason::MissingIngredients;
      return result;
    }
  }

  const player::InventoryInsertResult inserted =
      next.insert_stack(
          player::ItemStack{recipe.result.item, recipe.result.count},
          item_registry);
  if (!inserted.complete()) {
    result.reason = CraftingFailureReason::InventoryFull;
    return result;
  }

  inventory = std::move(next);
  result.reason = CraftingFailureReason::None;
  return result;
}

std::string crafting_failure_reason_name(CraftingFailureReason reason) {
  switch (reason) {
    case CraftingFailureReason::None:
      return "none";
    case CraftingFailureReason::UnknownRecipe:
      return "unknown_recipe";
    case CraftingFailureReason::RecipeNotUnlocked:
      return "recipe_not_unlocked";
    case CraftingFailureReason::MissingIngredients:
      return "missing_ingredients";
    case CraftingFailureReason::InventoryFull:
      return "inventory_full";
    case CraftingFailureReason::InvalidRequest:
      return "invalid_request";
    case CraftingFailureReason::StationRequired:
      return "station_required";
  }
  return "invalid_request";
}

} // namespace arv::crafting
