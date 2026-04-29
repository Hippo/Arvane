#pragma once

#include "arv/client/client_runtime.hpp"
#include "arv/platform/glfw_window.hpp"

namespace arv::desktop {

[[nodiscard]] client::InputFrame to_client_input(const platform::InputSnapshot& input);

} // namespace arv::desktop
