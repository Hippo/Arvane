#pragma once

#include "arv/render/render_types.hpp"

#include <optional>
#include <vector>

namespace arv::render {

class PagedTextureRegionAllocator {
public:
  struct Config {
    u16 page_width{2048};
    u16 page_height{2048};
    u16 max_pages{1};
  };

  PagedTextureRegionAllocator();
  explicit PagedTextureRegionAllocator(Config config);

  void configure(Config config);
  void clear() noexcept;

  [[nodiscard]] std::optional<TextureRegionHandle> allocate(u16 width, u16 height);
  void free(TextureRegionHandle handle);

  [[nodiscard]] std::optional<AtlasRegion> resolve(
      TextureRegionHandle handle) const noexcept;

  [[nodiscard]] u16 page_width() const noexcept {
    return config_.page_width;
  }

  [[nodiscard]] u16 page_height() const noexcept {
    return config_.page_height;
  }

  [[nodiscard]] u16 max_pages() const noexcept {
    return config_.max_pages;
  }

  [[nodiscard]] u64 allocated_pixels() const noexcept {
    return allocated_pixels_;
  }

  [[nodiscard]] u64 capacity_pixels() const noexcept {
    return static_cast<u64>(config_.page_width) * config_.page_height * config_.max_pages;
  }

  [[nodiscard]] f32 usage_ratio() const noexcept;

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

  struct RegionSlot {
    AtlasRegion region{};
    u32 generation{1U};
    bool occupied{};
  };

  [[nodiscard]] std::optional<FreeRect> allocate_from_page(
      Page& page,
      u16 width,
      u16 height);
  [[nodiscard]] TextureRegionHandle publish_region(AtlasRegion region);
  [[nodiscard]] bool validate_handle(TextureRegionHandle handle) const noexcept;
  void reset_pages();

  Config config_{};
  std::vector<Page> pages_{};
  std::vector<RegionSlot> slots_{{}};
  std::vector<u32> free_slot_ids_{};
  u64 allocated_pixels_{};
  u32 generation_{1U};
};

} // namespace arv::render
