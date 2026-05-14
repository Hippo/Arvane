#pragma once

#include "arv/base/ui/screens/screen.hpp"

#include <optional>
#include <string>

namespace arv::ui {

[[nodiscard]] std::unique_ptr<UIScreen> make_multi_player_menu_screen();
[[nodiscard]] std::unique_ptr<UIScreen> make_multiplayer_connecting_screen(
    ServerListEntry server);
[[nodiscard]] std::unique_ptr<UIScreen> make_multiplayer_disconnect_screen(
    std::string reason,
    std::optional<ServerListEntry> reconnect_server = std::nullopt);
[[nodiscard]] std::unique_ptr<UIScreen> make_add_server_screen();
[[nodiscard]] std::unique_ptr<UIScreen> make_edit_server_screen(
    usize server_index,
    ServerListEntry server);

} // namespace arv::ui
