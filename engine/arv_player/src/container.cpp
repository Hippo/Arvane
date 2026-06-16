#include "arv/player/container.hpp"

#include "arv/io/data_codecs.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string_view>
#include <utility>

namespace arv::player {
namespace {

template<typename T>
using ContainerResult = Expected<T, ContainerRegistryError>;

[[nodiscard]] ContainerRegistryError error(std::string message) {
  return ContainerRegistryError{std::move(message)};
}

[[nodiscard]] bool finite_positive(Vec2f value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         value.x > 0.0F && value.y > 0.0F;
}

[[nodiscard]] ContainerResult<std::string> read_required_string(
    const io::DataValue& object,
    std::string_view key,
    std::string_view context) {
  const io::DataValue* value = object.get(key);
  if (value == nullptr || value->kind() != io::DataKind::String) {
    return ContainerResult<std::string>::failure(
        error(std::string{context} + " requires string field " + std::string{key}));
  }
  std::string text{value->as_string_or({})};
  if (text.empty()) {
    return ContainerResult<std::string>::failure(
        error(std::string{context} + " field " + std::string{key} + " must not be empty"));
  }
  return ContainerResult<std::string>::success(std::move(text));
}

[[nodiscard]] ContainerResult<std::string> read_required_id_or_name(
    const io::DataValue& object,
    std::string_view context) {
  if (object.get("id") != nullptr) {
    return read_required_string(object, "id", context);
  }
  return read_required_string(object, "name", context);
}

[[nodiscard]] ContainerResult<u32> read_required_u32(
    const io::DataValue& object,
    std::string_view key,
    std::string_view context) {
  const io::DataValue* value = object.get(key);
  if (value == nullptr || !io::is_integer_value(*value)) {
    return ContainerResult<u32>::failure(
        error(std::string{context} + " requires integer field " + std::string{key}));
  }
  const i64 raw = value->as_i64_or(0);
  if (raw < 0 || raw > static_cast<i64>(std::numeric_limits<u32>::max())) {
    return ContainerResult<u32>::failure(
        error(std::string{context} + " field " + std::string{key} + " is out of range"));
  }
  return ContainerResult<u32>::success(static_cast<u32>(raw));
}

[[nodiscard]] ContainerResult<Vec2f> read_required_vec2f(
    const io::DataValue& object,
    std::string_view key,
    std::string_view context) {
  const io::DataValue* value = object.get(key);
  const io::DataValue::Array* array = value == nullptr ? nullptr : value->array_if();
  if (array == nullptr || array->size() != 2U ||
      !io::is_number_value((*array)[0]) || !io::is_number_value((*array)[1])) {
    return ContainerResult<Vec2f>::failure(
        error(std::string{context} + " requires numeric [x,y] field " + std::string{key}));
  }
  Vec2f parsed{
      static_cast<f32>((*array)[0].as_f64_or(0.0)),
      static_cast<f32>((*array)[1].as_f64_or(0.0)),
  };
  if (!std::isfinite(parsed.x) || !std::isfinite(parsed.y)) {
    return ContainerResult<Vec2f>::failure(
        error(std::string{context} + " field " + std::string{key} + " must be finite"));
  }
  return ContainerResult<Vec2f>::success(parsed);
}

[[nodiscard]] const io::DataValue* array_root(
    const io::DataValue& root,
    std::string_view key) {
  if (root.kind() != io::DataKind::Object) {
    return &root;
  }
  return root.get(key);
}

[[nodiscard]] bool id_exists(
    std::span<const ContainerTypeDefinition> definitions,
    std::string_view id) noexcept {
  return std::any_of(
      definitions.begin(),
      definitions.end(),
      [&](const ContainerTypeDefinition& definition) {
        return definition.id == id;
      });
}

[[nodiscard]] bool id_exists(
    std::span<const ContainerUiLayoutDefinition> definitions,
    std::string_view id) noexcept {
  return std::any_of(
      definitions.begin(),
      definitions.end(),
      [&](const ContainerUiLayoutDefinition& definition) {
        return definition.id == id;
      });
}

} // namespace

std::optional<ContainerSlotSource> container_slot_source_from_string(
    std::string_view value) noexcept {
  if (value == "container" || value == "opened_container") {
    return ContainerSlotSource::OpenedContainer;
  }
  if (value == "player_inventory" || value == "player") {
    return ContainerSlotSource::PlayerInventory;
  }
  return std::nullopt;
}

std::string_view container_slot_source_name(ContainerSlotSource source) noexcept {
  switch (source) {
    case ContainerSlotSource::OpenedContainer:
      return "container";
    case ContainerSlotSource::PlayerInventory:
      return "player_inventory";
  }
  return "container";
}

ContainerTypeRegistry::Result ContainerTypeRegistry::build(
    std::vector<ContainerTypeDefinition> definitions) {
  ContainerTypeRegistry registry;
  registry.definitions_.reserve(definitions.size());
  for (ContainerTypeDefinition& definition : definitions) {
    if (definition.id.empty()) {
      return Result::failure(error("container type id must not be empty"));
    }
    if (definition.slot_count == 0U) {
      return Result::failure(error("container type " + definition.id + " slot_count must be positive"));
    }
    if (definition.title.empty()) {
      definition.title = definition.id;
    }
    if (definition.ui_layout.empty()) {
      return Result::failure(error("container type " + definition.id + " requires ui_layout"));
    }
    if (id_exists(registry.definitions_, definition.id)) {
      return Result::failure(error("duplicate container type id: " + definition.id));
    }
    registry.definitions_.push_back(std::move(definition));
  }
  return Result::success(std::move(registry));
}

const ContainerTypeDefinition* ContainerTypeRegistry::find(
    std::string_view id) const noexcept {
  const auto found = std::find_if(
      definitions_.begin(),
      definitions_.end(),
      [&](const ContainerTypeDefinition& definition) {
        return definition.id == id;
      });
  return found == definitions_.end() ? nullptr : &*found;
}

std::span<const ContainerTypeDefinition> ContainerTypeRegistry::definitions() const noexcept {
  return definitions_;
}

ContainerUiLayoutRegistry::Result ContainerUiLayoutRegistry::build(
    std::vector<ContainerUiLayoutDefinition> definitions) {
  ContainerUiLayoutRegistry registry;
  registry.definitions_.reserve(definitions.size());
  for (ContainerUiLayoutDefinition& definition : definitions) {
    if (definition.id.empty()) {
      return Result::failure(error("container UI layout id must not be empty"));
    }
    if (!finite_positive(definition.size)) {
      return Result::failure(error("container UI layout " + definition.id + " size must be positive"));
    }
    if (definition.slot_groups.empty()) {
      return Result::failure(error("container UI layout " + definition.id + " requires slot_groups"));
    }
    for (ContainerSlotGroupDefinition& group : definition.slot_groups) {
      if (group.id.empty()) {
        group.id = container_slot_source_name(group.source);
      }
      if (group.slot_count == 0U) {
        return Result::failure(error("container UI layout " + definition.id + " has empty slot group"));
      }
      if (group.columns == 0U) {
        return Result::failure(error("container UI layout " + definition.id + " slot group columns must be positive"));
      }
      if (!finite_positive(group.slot_spacing)) {
        return Result::failure(error("container UI layout " + definition.id + " slot_spacing must be positive"));
      }
    }
    if (id_exists(registry.definitions_, definition.id)) {
      return Result::failure(error("duplicate container UI layout id: " + definition.id));
    }
    registry.definitions_.push_back(std::move(definition));
  }
  return Result::success(std::move(registry));
}

const ContainerUiLayoutDefinition* ContainerUiLayoutRegistry::find(
    std::string_view id) const noexcept {
  const auto found = std::find_if(
      definitions_.begin(),
      definitions_.end(),
      [&](const ContainerUiLayoutDefinition& definition) {
        return definition.id == id;
      });
  return found == definitions_.end() ? nullptr : &*found;
}

std::span<const ContainerUiLayoutDefinition> ContainerUiLayoutRegistry::definitions() const noexcept {
  return definitions_;
}

ContainerTypeDefinitionsResult parse_container_type_definitions_json(
    std::string_view text) {
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return ContainerTypeDefinitionsResult::failure(
        error("failed to parse container type JSON: " + decoded.error().describe()));
  }
  const io::DataValue* root = array_root(decoded.value().root, "container_types");
  const io::DataValue::Array* array = root == nullptr ? nullptr : root->array_if();
  if (array == nullptr) {
    return ContainerTypeDefinitionsResult::failure(error("container type JSON must contain container_types array"));
  }

  std::vector<ContainerTypeDefinition> definitions;
  definitions.reserve(array->size());
  for (const io::DataValue& item : *array) {
    if (item.kind() != io::DataKind::Object) {
      return ContainerTypeDefinitionsResult::failure(error("container type entry must be an object"));
    }
    ContainerTypeDefinition definition;
    auto id = read_required_id_or_name(item, "container type");
    auto slots = read_required_u32(item, "slot_count", "container type");
    auto title = read_required_string(item, "title", "container type");
    auto layout = read_required_string(item, "ui_layout", "container type");
    if (!id) {
      return ContainerTypeDefinitionsResult::failure(id.error());
    }
    if (!slots) {
      return ContainerTypeDefinitionsResult::failure(slots.error());
    }
    if (!title) {
      return ContainerTypeDefinitionsResult::failure(title.error());
    }
    if (!layout) {
      return ContainerTypeDefinitionsResult::failure(layout.error());
    }
    definition.id = std::move(id.value());
    definition.slot_count = slots.value();
    definition.title = std::move(title.value());
    definition.ui_layout = std::move(layout.value());
    definitions.push_back(std::move(definition));
  }
  return ContainerTypeDefinitionsResult::success(std::move(definitions));
}

ContainerUiLayoutDefinitionsResult parse_container_ui_layout_definitions_json(
    std::string_view text) {
  auto decoded = io::decode_json_value(text);
  if (!decoded) {
    return ContainerUiLayoutDefinitionsResult::failure(
        error("failed to parse container UI layout JSON: " + decoded.error().describe()));
  }
  const io::DataValue* root = array_root(decoded.value().root, "container_layouts");
  const io::DataValue::Array* array = root == nullptr ? nullptr : root->array_if();
  if (array == nullptr) {
    return ContainerUiLayoutDefinitionsResult::failure(
        error("container UI layout JSON must contain container_layouts array"));
  }

  std::vector<ContainerUiLayoutDefinition> definitions;
  definitions.reserve(array->size());
  for (const io::DataValue& item : *array) {
    if (item.kind() != io::DataKind::Object) {
      return ContainerUiLayoutDefinitionsResult::failure(error("container UI layout entry must be an object"));
    }
    auto id = read_required_id_or_name(item, "container UI layout");
    auto size = read_required_vec2f(item, "size", "container UI layout");
    if (!id) {
      return ContainerUiLayoutDefinitionsResult::failure(id.error());
    }
    if (!size) {
      return ContainerUiLayoutDefinitionsResult::failure(size.error());
    }

    ContainerUiLayoutDefinition definition;
    definition.id = std::move(id.value());
    definition.size = size.value();

    const io::DataValue* groups = item.get("slot_groups");
    const io::DataValue::Array* group_array =
        groups == nullptr ? nullptr : groups->array_if();
    if (group_array == nullptr) {
      return ContainerUiLayoutDefinitionsResult::failure(
          error("container UI layout " + definition.id + " requires slot_groups array"));
    }
    definition.slot_groups.reserve(group_array->size());
    for (const io::DataValue& group_json : *group_array) {
      if (group_json.kind() != io::DataKind::Object) {
        return ContainerUiLayoutDefinitionsResult::failure(error("container UI slot group must be an object"));
      }
      auto source_text = read_required_string(group_json, "source", "container UI slot group");
      auto start_slot = read_required_u32(group_json, "start_slot", "container UI slot group");
      auto slot_count = read_required_u32(group_json, "slot_count", "container UI slot group");
      auto columns = read_required_u32(group_json, "columns", "container UI slot group");
      auto position = read_required_vec2f(group_json, "position", "container UI slot group");
      auto slot_spacing = read_required_vec2f(group_json, "slot_spacing", "container UI slot group");
      if (!source_text) {
        return ContainerUiLayoutDefinitionsResult::failure(source_text.error());
      }
      if (!start_slot) {
        return ContainerUiLayoutDefinitionsResult::failure(start_slot.error());
      }
      if (!slot_count) {
        return ContainerUiLayoutDefinitionsResult::failure(slot_count.error());
      }
      if (!columns) {
        return ContainerUiLayoutDefinitionsResult::failure(columns.error());
      }
      if (!position) {
        return ContainerUiLayoutDefinitionsResult::failure(position.error());
      }
      if (!slot_spacing) {
        return ContainerUiLayoutDefinitionsResult::failure(slot_spacing.error());
      }
      const std::optional<ContainerSlotSource> source =
          container_slot_source_from_string(source_text.value());
      if (!source.has_value()) {
        return ContainerUiLayoutDefinitionsResult::failure(
            error("invalid container UI slot source: " + source_text.value()));
      }

      ContainerSlotGroupDefinition group;
      if (const io::DataValue* group_id = group_json.get("id");
          group_id != nullptr) {
        if (group_id->kind() != io::DataKind::String) {
          return ContainerUiLayoutDefinitionsResult::failure(
              error("container UI slot group id must be a string"));
        }
        group.id = std::string{group_id->as_string_or({})};
      }
      group.source = source.value();
      group.start_slot = start_slot.value();
      group.slot_count = slot_count.value();
      group.columns = columns.value();
      group.position = position.value();
      group.slot_spacing = slot_spacing.value();
      definition.slot_groups.push_back(std::move(group));
    }
    definitions.push_back(std::move(definition));
  }
  return ContainerUiLayoutDefinitionsResult::success(std::move(definitions));
}

} // namespace arv::player
