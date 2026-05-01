#pragma once

#include "arv/ui/screens/screen.hpp"

namespace arv::ui {

[[nodiscard]] std::unique_ptr<UIScreen> make_settings_menu_screen();
[[nodiscard]] std::unique_ptr<UIScreen> make_render_settings_screen();
[[nodiscard]] std::unique_ptr<UIScreen> make_key_binds_screen();
[[nodiscard]] std::unique_ptr<UIScreen> make_other_settings_screen();

} // namespace arv::ui
