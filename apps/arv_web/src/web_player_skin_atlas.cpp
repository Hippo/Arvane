#include "web_player_skin_atlas.hpp"

#include <algorithm>
#include <limits>

namespace arv::web {

void WebPlayerSkinRegionAllocator::configure(WebPlayerSkinAtlasConfig config) {
  config.page_width = std::max<u16>(1U, config.page_width);
  config.page_height = std::max<u16>(1U, config.page_height);
  config.max_pages = std::max<u16>(1U, config.max_pages);
  config_ = config;
  clear();
}

void WebPlayerSkinRegionAllocator::clear() noexcept {
  pages_.clear();
  regions_.clear();
  if (generation_ < std::numeric_limits<u32>::max()) {
    ++generation_;
  } else {
    generation_ = 1U;
  }
}

std::optional<render::TextureRegionHandle> WebPlayerSkinRegionAllocator::allocate(
    u16 width,
    u16 height) {
  if (width == 0U || height == 0U ||
      width > config_.page_width ||
      height > config_.page_height) {
    return std::nullopt;
  }

  for (u16 page_index = 0U; page_index < pages_.size(); ++page_index) {
    if (auto region = allocate_from_page(pages_[page_index], page_index, width, height)) {
      regions_.push_back({region.value(), generation_, true});
      return render::TextureRegionHandle{
          static_cast<u32>(regions_.size()),
          generation_};
    }
  }

  if (pages_.size() >= config_.max_pages) {
    return std::nullopt;
  }

  Page page;
  page.free_rects.push_back({0U, 0U, config_.page_width, config_.page_height});
  pages_.push_back(std::move(page));
  auto region = allocate_from_page(
      pages_.back(),
      static_cast<u16>(pages_.size() - 1U),
      width,
      height);
  if (!region.has_value()) {
    return std::nullopt;
  }
  regions_.push_back({region.value(), generation_, true});
  return render::TextureRegionHandle{
      static_cast<u32>(regions_.size()),
      generation_};
}

std::optional<render::AtlasRegion> WebPlayerSkinRegionAllocator::resolve(
    render::TextureRegionHandle handle) const noexcept {
  if (!handle.valid() ||
      handle.generation != generation_ ||
      handle.id == 0U ||
      handle.id > regions_.size()) {
    return std::nullopt;
  }

  const RegionEntry& entry = regions_[handle.id - 1U];
  if (!entry.occupied || entry.generation != handle.generation) {
    return std::nullopt;
  }
  return entry.region;
}

std::optional<render::AtlasRegion> WebPlayerSkinRegionAllocator::allocate_from_page(
    Page& page,
    u16 page_index,
    u16 width,
    u16 height) {
  for (auto rect = page.free_rects.begin(); rect != page.free_rects.end(); ++rect) {
    if (width > rect->width || height > rect->height) {
      continue;
    }

    const FreeRect used = *rect;
    page.free_rects.erase(rect);
    if (used.width > width) {
      page.free_rects.push_back({
          static_cast<u16>(used.x + width),
          used.y,
          static_cast<u16>(used.width - width),
          height});
    }
    if (used.height > height) {
      page.free_rects.push_back({
          used.x,
          static_cast<u16>(used.y + height),
          used.width,
          static_cast<u16>(used.height - height)});
    }
    return render::AtlasRegion{page_index, used.x, used.y, width, height};
  }
  return std::nullopt;
}

} // namespace arv::web
