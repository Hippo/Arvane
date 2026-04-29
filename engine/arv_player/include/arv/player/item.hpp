#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/id.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::player {

struct ItemTag {};
using ItemID = ID<ItemTag, u32>;

inline constexpr ItemID AirItemID{0};
inline constexpr std::string_view AirItemName = "arvane:air";
inline constexpr std::string_view MissingItemName = "arvane:missing_item";

enum class ItemRenderType : u8 {
  Texture,
  Model,
  Block,
};

struct ItemRegistryError {
  std::string message{};
};

struct ItemDefinition {
  std::string name{};
  std::string display_name{};
  std::string category{};
  u16 max_stack_size{64};
  std::string places_block{};
  std::string texture_asset{};
  std::string model_asset{};
  std::optional<ItemRenderType> explicit_render_type{};
  bool generated_from_block{};
  std::optional<voxel::BlockVariantID> placement_variant{};
  std::optional<voxel::BlockID> source_block{};
};

struct ItemRecord {
  ItemID id{};
  std::string name{};
  std::string display_name{};
  std::string category{};
  u16 max_stack_size{64};
  ItemRenderType render_type{ItemRenderType::Texture};
  std::string texture_asset{};
  std::string model_asset{};
  std::optional<voxel::BlockVariantID> placement_variant{};
  std::optional<voxel::BlockVariantID> render_block_variant{};
  std::optional<voxel::BlockID> source_block{};
};

struct ItemStack {
  ItemID item{};
  u16 count{};

  constexpr bool operator==(const ItemStack&) const = default;
};

class ItemRegistry {
public:
  using Result = Expected<ItemRegistry, ItemRegistryError>;

  [[nodiscard]] static Result build(std::vector<ItemDefinition> definitions);
  [[nodiscard]] static Result build_default_block_items(const voxel::BlockRegistry& registry);
  [[nodiscard]] static Result build_from_blocks_and_items(
      const voxel::BlockRegistry& registry,
      std::vector<ItemDefinition> explicit_definitions);

  [[nodiscard]] const ItemRecord* item(ItemID id) const noexcept;
  [[nodiscard]] std::optional<ItemID> find_item(std::string_view name) const;
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
[[nodiscard]] Expected<ItemRenderType, ItemRegistryError> parse_item_render_type(std::string_view value);
[[nodiscard]] Expected<std::vector<ItemDefinition>, ItemRegistryError> parse_item_definitions_json(
    std::string_view text);

} // namespace arv::player
