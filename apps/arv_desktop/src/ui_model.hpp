#pragma once

#include "arv/server/server_runtime.hpp"
#include "arv/base/ui.hpp"

#include <vector>

namespace arv::desktop {

[[nodiscard]] std::vector<ui::PlayerListEntryView> player_list_for_ui(
    const server::ServerRuntime& server);

} // namespace arv::desktop
