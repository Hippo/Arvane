#pragma once

#include "arv/ui/screens/screen.hpp"

namespace arv::ui {

[[nodiscard]] std::unique_ptr<UIScreen> make_multi_player_menu_screen();
[[nodiscard]] std::unique_ptr<UIScreen> make_add_server_screen();
[[nodiscard]] std::unique_ptr<UIScreen> make_edit_server_screen(
    usize server_index,
    ServerListEntry server);

} // namespace arv::ui
