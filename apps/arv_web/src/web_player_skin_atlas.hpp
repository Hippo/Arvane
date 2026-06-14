#pragma once

#include "arv/core/types.hpp"
#include "arv/render/render_types.hpp"

#include <optional>
#include <vector>

namespace arv::web {

struct WebPlayerSkinAtlasConfig {
  u16 page_width{2048};
  u16 page_height{2048};
  u16 max_pages{8};
};

class WebPlayerSkinRegionAllocator {
public:
  void configure(WebPlayerSkinAtlasConfig config);
  void clear() noexcept;

  [[nodiscard]] std::optional<render::TextureRegionHandle> allocate(u16 width, u16 height);
  [[nodiscard]] std::optional<render::AtlasRegion> resolve(
      render::TextureRegionHandle handle) const noexcept;

  [[nodiscard]] u16 page_width() const noexcept {
    return config_.page_width;
  }

  [[nodiscard]] u16 page_height() const noexcept {
    return config_.page_height;
  }

  [[nodiscard]] u16 max_pages() const noexcept {
    return config_.max_pages;
  }

private:
  struct FreeRect {
    u16 x{};
    u16 y{};
    u16 width{};
    u16 height{};
  };

  struct Page {
    std::vector<FreeRect> free_rects{};
  };

  struct RegionEntry {
    render::AtlasRegion region{};
    u32 generation{};
    bool occupied{};
  };

  [[nodiscard]] std::optional<render::AtlasRegion> allocate_from_page(
      Page& page,
      u16 page_index,
      u16 width,
      u16 height);

  WebPlayerSkinAtlasConfig config_{};
  std::vector<Page> pages_{};
  std::vector<RegionEntry> regions_{};
  u32 generation_{1U};
};

} // namespace arv::web
