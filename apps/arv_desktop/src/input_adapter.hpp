#pragma once

#include "arv/client/client_runtime.hpp"
#include "arv/base/ui/screens/screen.hpp"
#include "arv/platform/glfw_window.hpp"

#include <string>

namespace arv::desktop {

[[nodiscard]] client::InputFrame to_client_input(const platform::InputSnapshot& input);
[[nodiscard]] ui::InputBindingDevice ui_device_from(
    platform::InputDeviceType type) noexcept;
[[nodiscard]] std::string binding_label(const platform::InputBinding& binding);
[[nodiscard]] ui::CapturedInputView captured_input_from(
    const platform::RawInputEvent& event);

} // namespace arv::desktop
