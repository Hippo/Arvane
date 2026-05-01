#pragma once

#include "arv/ui/screens/screen.hpp"

namespace arv::ui {

[[nodiscard]] std::unique_ptr<UIScreen> make_single_player_menu_screen();
[[nodiscard]] std::unique_ptr<UIScreen> make_create_world_screen();

} // namespace arv::ui
