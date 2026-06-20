#pragma once

#include "arv/base/ui/screens/screen.hpp"
#include "arv/client/local_player_controller.hpp"
#include "arv/platform/input_snapshot.hpp"

#include <string>
#include <vector>

namespace arv::client {

[[nodiscard]] core::TextEditCommand text_edit_command_from_platform(
    platform::TextEditCommand command) noexcept;

[[nodiscard]] std::vector<core::TextEditInput> text_edit_events_from_platform(
    const std::vector<platform::TextEditEvent>& events);

[[nodiscard]] InputFrame input_frame_from_platform_snapshot(
    const platform::InputSnapshot& input);

void clear_platform_input_frame_edges(platform::InputSnapshot& input) noexcept;

void clear_platform_input_captured_binding_edges(
    platform::InputSnapshot& input) noexcept;

[[nodiscard]] ui::InputBindingDevice ui_input_device_from_platform(
    platform::InputDeviceType type) noexcept;

[[nodiscard]] std::string input_binding_label(const platform::InputBinding& binding);

[[nodiscard]] std::string first_action_binding_label(
    const platform::InputBindings& input_bindings,
    platform::LogicalAction action);

[[nodiscard]] ui::CapturedInputView captured_input_from_platform(
    const platform::RawInputEvent& event);

} // namespace arv::client
