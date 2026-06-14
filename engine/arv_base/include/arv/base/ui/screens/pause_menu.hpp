#pragma once

#include "arv/base/ui/screens/screen.hpp"

namespace arv::ui {

struct PauseMenuOptions {
  bool show_quit{true};
};

[[nodiscard]] std::unique_ptr<UIScreen> make_gameplay_screen(
    PauseMenuOptions options = {});
[[nodiscard]] std::unique_ptr<UIScreen> make_pause_menu_screen(
    PauseMenuOptions options = {});

} // namespace arv::ui
