#pragma once

#include "arv/core/expected.hpp"
#include "arv/core/math.hpp"
#include "arv/core/types.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::player {

struct ContainerRegistryError {
  std::string message{};
};

enum class ContainerSlotSource : u8 {
  OpenedContainer,
  PlayerInventory,
};

struct ContainerTypeDefinition {
  std::string id{};
  u32 slot_count{};
  std::string title{};
  std::string ui_layout{};
};

struct ContainerSlotGroupDefinition {
  std::string id{};
  ContainerSlotSource source{ContainerSlotSource::OpenedContainer};
  u32 start_slot{};
  u32 slot_count{};
  u32 columns{1};
  Vec2f position{};
  Vec2f slot_spacing{44.0F, 44.0F};
};

struct ContainerUiLayoutDefinition {
  std::string id{};
  Vec2f size{};
  std::vector<ContainerSlotGroupDefinition> slot_groups{};
};

using ContainerTypeDefinitionsResult =
    Expected<std::vector<ContainerTypeDefinition>, ContainerRegistryError>;
using ContainerUiLayoutDefinitionsResult =
    Expected<std::vector<ContainerUiLayoutDefinition>, ContainerRegistryError>;

class ContainerTypeRegistry {
public:
  using Result = Expected<ContainerTypeRegistry, ContainerRegistryError>;

  [[nodiscard]] static Result build(std::vector<ContainerTypeDefinition> definitions);

  [[nodiscard]] const ContainerTypeDefinition* find(std::string_view id) const noexcept;
  [[nodiscard]] std::span<const ContainerTypeDefinition> definitions() const noexcept;

private:
  std::vector<ContainerTypeDefinition> definitions_{};
};

class ContainerUiLayoutRegistry {
public:
  using Result = Expected<ContainerUiLayoutRegistry, ContainerRegistryError>;

  [[nodiscard]] static Result build(std::vector<ContainerUiLayoutDefinition> definitions);

  [[nodiscard]] const ContainerUiLayoutDefinition* find(std::string_view id) const noexcept;
  [[nodiscard]] std::span<const ContainerUiLayoutDefinition> definitions() const noexcept;

private:
  std::vector<ContainerUiLayoutDefinition> definitions_{};
};

[[nodiscard]] std::optional<ContainerSlotSource> container_slot_source_from_string(
    std::string_view value) noexcept;
[[nodiscard]] std::string_view container_slot_source_name(ContainerSlotSource source) noexcept;

[[nodiscard]] ContainerTypeDefinitionsResult parse_container_type_definitions_json(
    std::string_view text);
[[nodiscard]] ContainerUiLayoutDefinitionsResult parse_container_ui_layout_definitions_json(
    std::string_view text);

} // namespace arv::player
