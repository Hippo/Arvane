#pragma once

#include "arv/asset/resource.hpp"
#include "arv/asset/texture_atlas.hpp"
#include "arv/render/render_types.hpp"

namespace arv::client {

[[nodiscard]] render::TextureUploadData texture_upload_from_image(
    const asset::Image& image);

[[nodiscard]] render::TextureArrayUploadData texture_array_upload_from_image(
    const asset::Image& image);

[[nodiscard]] render::TextureArrayUploadData texture_array_upload_from_atlas(
    const asset::TextureAtlas& atlas);

} // namespace arv::client
