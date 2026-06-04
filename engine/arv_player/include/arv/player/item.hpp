#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/id.hpp"
#include "arv/core/types.hpp"
#include "arv/io/data_document.hpp"
#include "arv/voxel/block.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace arv::player {

struct ItemTag {};
using ItemID = ID<ItemTag, u32>;

inline constexpr ItemID AirItemID{0};
inline constexpr std::string_view AirItemName = "arvane:air";
inline constexpr std::string_view MissingItemName = "arvane:missing_item";
inline constexpr f32 DefaultItemBuoyancy = 1.0F;
inline constexpr u32 MaxItemStackDataBytes = 16U * 1024U;

enum class ItemRenderType : u8 {
  Texture,
  Model,
  Block,
};

enum class HeldItemAnimationSelection : u8 {
  Default,
  Named,
  Disabled,
};

enum class HeldItemAnimationPlayCondition : u8 {
  Always,
  PlacementSucceeds,
};

struct HeldItemAnimationBinding {
  HeldItemAnimationSelection selection{HeldItemAnimationSelection::Default};
  std::string animation{};
  HeldItemAnimationPlayCondition play_condition{HeldItemAnimationPlayCondition::Always};
};

struct ItemRegistryError {
  std::string message{};
};

struct ItemAttackData {
  f32 damage_amount{};
  f32 attack_range{};
  u32 cooldown_ticks{};
  f32 horizontal_knockback{};
  f32 vertical_knockback{};
  f32 sprint_knockback_bonus{};

  constexpr bool operator==(const ItemAttackData&) const = default;
};

struct ItemDefinition {
  std::string name{};
  std::string display_name{};
  std::string behavior_id{};
  std::string category{};
  std::vector<std::string> tags{};
  u16 max_stack_size{64};
  std::string places_block{};
  std::string texture_asset{};
  std::string model_asset{};
  std::optional<ItemRenderType> explicit_render_type{};
  bool generated_from_block{};
  std::optional<voxel::BlockVariantID> placement_variant{};
  std::optional<voxel::BlockID> source_block{};
  f32 buoyancy{DefaultItemBuoyancy};
  bool buoyancy_set{};
  std::string tool_category{};
  f32 tool_break_speed_multiplier{1.0F};
  std::optional<ItemAttackData> attack_data{};
  HashMap<std::string, HeldItemAnimationBinding> held_animations{};
};

struct ItemRecord {
  ItemID id{};
  std::string name{};
  std::string display_name{};
  std::string behavior_id{};
  std::string category{};
  std::vector<std::string> tags{};
  u16 max_stack_size{64};
  ItemRenderType render_type{ItemRenderType::Texture};
  std::string texture_asset{};
  std::string model_asset{};
  std::optional<voxel::BlockVariantID> placement_variant{};
  std::optional<voxel::BlockVariantID> render_block_variant{};
  std::optional<voxel::BlockID> source_block{};
  f32 buoyancy{DefaultItemBuoyancy};
  std::string tool_category{};
  f32 tool_break_speed_multiplier{1.0F};
  std::optional<ItemAttackData> attack_data{};
  HashMap<std::string, HeldItemAnimationBinding> held_animations{};
};

struct ItemStack {
  ItemID item{};
  u16 count{};
  std::optional<io::DataDocument> data{};

  [[nodiscard]] bool operator==(const ItemStack& other) const;
};

struct ItemBehaviorHandler {};

class ItemBehaviorRegistry {
public:
  using Result = Expected<void, ItemRegistryError>;

  [[nodiscard]] Result register_handler(std::string id, ItemBehaviorHandler handler = {});
  [[nodiscard]] const ItemBehaviorHandler* find(std::string_view id) const noexcept;
  [[nodiscard]] bool contains(std::string_view id) const noexcept;

private:
  std::vector<std::pair<std::string, ItemBehaviorHandler>> handlers_;
};

struct ItemRegistryBuildOptions {
  FunctionRef<bool(std::string_view)> behavior_exists{};
  std::string default_block_item_behavior_id{};
};

class ItemRegistry {
public:
  using Result = Expected<ItemRegistry, ItemRegistryError>;

  [[nodiscard]] static Result build(
      std::vector<ItemDefinition> definitions,
      ItemRegistryBuildOptions options = {});
  [[nodiscard]] static Result build_default_block_items(
      const voxel::BlockRegistry& registry,
      ItemRegistryBuildOptions options = {});
  [[nodiscard]] static Result build_from_blocks_and_items(
      const voxel::BlockRegistry& registry,
      std::vector<ItemDefinition> explicit_definitions,
      ItemRegistryBuildOptions options = {});

  [[nodiscard]] const ItemRecord* item(ItemID id) const noexcept;
  [[nodiscard]] std::optional<ItemID> find_item(std::string_view name) const;
  void set_display_name(ItemID id, std::string display_name);
  [[nodiscard]] usize item_count() const noexcept {
    return items_.size();
  }
  [[nodiscard]] bool is_air(ItemID id) const noexcept {
    return id == AirItemID;
  }
  [[nodiscard]] const ItemRecord* missing_item() const noexcept;

private:
  std::vector<ItemRecord> items_{};
  HashMap<std::string, ItemID> name_to_id_{};
  std::optional<ItemID> missing_item_id_{};
};

[[nodiscard]] ItemDefinition make_air_item_definition();
[[nodiscard]] ItemDefinition make_missing_item_definition();
[[nodiscard]] bool item_stack_data_equal(
    const std::optional<io::DataDocument>& lhs,
    const std::optional<io::DataDocument>& rhs) noexcept;
[[nodiscard]] bool item_stack_has_data(const ItemStack& stack) noexcept;
void normalize_item_stack_data(ItemStack& stack) noexcept;
[[nodiscard]] ItemStack item_stack_with_count(const ItemStack& stack, u16 count);
[[nodiscard]] Expected<ItemRenderType, ItemRegistryError> parse_item_render_type(std::string_view value);
[[nodiscard]] Expected<std::vector<ItemDefinition>, ItemRegistryError> parse_item_definitions_json(
    std::string_view text);

} // namespace arv::player
