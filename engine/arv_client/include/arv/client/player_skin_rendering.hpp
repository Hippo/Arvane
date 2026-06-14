#pragma once

#include "arv/asset/resource.hpp"
#include "arv/client/local_identity.hpp"
#include "arv/protocol/skins.hpp"

#include <filesystem>
#include <optional>
#include <string_view>

namespace arv::client {

[[nodiscard]] std::filesystem::path player_skin_render_path(
    const LocalPlayerIdentity& identity,
    std::string_view skin_hash);

[[nodiscard]] std::optional<asset::Image> load_valid_player_skin_render_image(
    const LocalPlayerIdentity& identity,
    const protocol::SkinDescriptor& skin);

[[nodiscard]] std::optional<asset::Image> load_valid_local_profile_skin_render_image(
    const std::filesystem::path& arvane_home,
    const LocalProfileSkin& skin);

} // namespace arv::client
