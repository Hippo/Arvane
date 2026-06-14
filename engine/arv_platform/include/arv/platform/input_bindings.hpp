#pragma once

#include "arv/core/types.hpp"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace arv::platform {

enum class LogicalAction : u8 {
  MoveForward,
  MoveBackward,
  MoveLeft,
  MoveRight,
  Jump,
  Sneak,
  Sprint,
  Mount,
  VehicleRollLeft,
  VehicleRollRight,
  PrimaryInteract,
  SecondaryInteract,
  Hotbar1,
  Hotbar2,
  Hotbar3,
  Hotbar4,
  Hotbar5,
  Hotbar6,
  Hotbar7,
  Hotbar8,
  Hotbar9,
  NextHotbarSlot,
  PrevHotbarSlot,
  OpenInventory,
  OpenChat,
  ShowTabMenu,
  TogglePauseMenu,
  ToggleDebugMenu,
  Count,
};

enum class InputDeviceType : u8 {
  Keyboard,
  MouseButton,
  MouseWheel,
};

enum class InputModifier : u8 {
  Shift = 1U << 0U,
  Control = 1U << 1U,
  Alt = 1U << 2U,
};

[[nodiscard]] constexpr u8 input_modifier_mask(InputModifier modifier) noexcept {
  return static_cast<u8>(modifier);
}

[[nodiscard]] constexpr bool has_input_modifier(u8 modifiers, InputModifier modifier) noexcept {
  return (modifiers & input_modifier_mask(modifier)) != 0U;
}

struct InputBinding {
  InputDeviceType device_type{InputDeviceType::Keyboard};
  std::string input_code{};
  u8 modifiers{};

  bool operator==(const InputBinding&) const = default;
};

inline constexpr u32 CurrentInputBindingsVersion = 1U;

using ActionBindingList = std::vector<InputBinding>;

struct InputBindings {
  u32 version{CurrentInputBindingsVersion};
  std::array<ActionBindingList, static_cast<usize>(LogicalAction::Count)> actions{};

  bool operator==(const InputBindings&) const = default;
};

[[nodiscard]] constexpr usize logical_action_index(LogicalAction action) noexcept {
  return static_cast<usize>(action);
}

[[nodiscard]] const ActionBindingList& bindings_for_action(
    const InputBindings& bindings,
    LogicalAction action) noexcept;
ActionBindingList& bindings_for_action(InputBindings& bindings, LogicalAction action) noexcept;

[[nodiscard]] std::string_view logical_action_name(LogicalAction action) noexcept;
[[nodiscard]] std::optional<LogicalAction> logical_action_from_name(std::string_view name) noexcept;

[[nodiscard]] std::string_view input_device_type_name(InputDeviceType type) noexcept;
[[nodiscard]] std::optional<InputDeviceType> input_device_type_from_name(std::string_view name) noexcept;

[[nodiscard]] std::string_view input_modifier_name(InputModifier modifier) noexcept;
[[nodiscard]] std::optional<InputModifier> input_modifier_from_name(std::string_view name) noexcept;

[[nodiscard]] std::optional<std::string> canonical_input_code(
    InputDeviceType type,
    std::string_view code) noexcept;
[[nodiscard]] std::string input_binding_label(const InputBinding& binding);
[[nodiscard]] bool is_valid_input_binding(const InputBinding& binding) noexcept;
[[nodiscard]] bool is_required_logical_action(LogicalAction action) noexcept;
[[nodiscard]] InputBindings default_input_bindings();

} // namespace arv::platform
