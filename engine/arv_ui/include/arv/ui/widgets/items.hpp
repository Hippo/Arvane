#pragma once

#include "arv/player/item.hpp"
#include "arv/ui/core/ui.hpp"
#include "arv/voxel/block.hpp"

#include <array>
#include <string>

namespace arv::ui {

struct UiItemStackView {
  std::string item_name{};
  std::string display_name{};
  u16 count{};
  u16 durability{};
  u16 max_durability{};
  player::ItemRenderType render_type{player::ItemRenderType::Texture};
  UiImageData texture_icon{};
  std::array<UiImageData, voxel::BlockFaceCount> block_faces{};

  [[nodiscard]] bool empty() const noexcept {
    return item_name.empty() || count == 0U;
  }
};

} // namespace arv::ui
