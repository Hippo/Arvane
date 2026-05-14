#pragma once

#include "arv/base/ui/screens/screen.hpp"

namespace arv::ui {

[[nodiscard]] std::unique_ptr<UIScreen> make_gameplay_screen();
[[nodiscard]] std::unique_ptr<UIScreen> make_pause_menu_screen();

} // namespace arv::ui
