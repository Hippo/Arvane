#pragma once

#include "arv/asset/resource.hpp"

#include <string>

namespace arv::web {

void reset_web_player_skin_render_assets();
[[nodiscard]] bool upload_web_player_skin_texture(
    std::string skin_hash,
    const asset::Image& image);
void refresh_web_player_skin_render_assets();

} // namespace arv::web
