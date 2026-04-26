#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/id.hpp"
#include "arv/core/types.hpp"
#include "arv/voxel/block.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace arv::player {

struct ItemTag {};
using ItemID = ID<ItemTag, u32>;

inline constexpr ItemID AirItemID{0};
inline constexpr std::string_view AirItemName = "arvane:air";
inline constexpr std::string_view MissingItemName = "arvane:missing_item";

struct ItemRegistryError {
  std::string message{};
};

struct ItemDefinition {
  std::string name{};
  std::string display_name{};
  u16 max_stack_size{64};
  std::optional<voxel::BlockVariantID> placement_variant{};
};

struct ItemRecord {
  ItemID id{};
  std::string name{};
  std::string display_name{};
  u16 max_stack_size{64};
  std::optional<voxel::BlockVariantID> placement_variant{};
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
  std::unordered_map<std::string, ItemID> name_to_id_{};
  std::optional<ItemID> missing_item_id_{};
};

[[nodiscard]] ItemDefinition make_air_item_definition();
[[nodiscard]] ItemDefinition make_missing_item_definition();

} // namespace arv::player
