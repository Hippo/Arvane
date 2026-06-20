#include "arv/render/paged_texture_region_allocator.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

namespace arv::render {
namespace {

[[nodiscard]] u16 positive_or_one(u16 value) noexcept {
  return std::max<u16>(value, 1U);
}

[[nodiscard]] u32 next_generation(u32 generation) noexcept {
  return generation < std::numeric_limits<u32>::max() ? generation + 1U : 1U;
}

} // namespace

PagedTextureRegionAllocator::PagedTextureRegionAllocator()
    : PagedTextureRegionAllocator(Config{}) {
}

PagedTextureRegionAllocator::PagedTextureRegionAllocator(Config config) {
  configure(config);
}

void PagedTextureRegionAllocator::configure(Config config) {
  config.page_width = positive_or_one(config.page_width);
  config.page_height = positive_or_one(config.page_height);
  config.max_pages = positive_or_one(config.max_pages);
  config_ = config;
  clear();
}

void PagedTextureRegionAllocator::clear() noexcept {
  generation_ = next_generation(generation_);
  slots_.clear();
  slots_.push_back({});
  free_slot_ids_.clear();
  allocated_pixels_ = 0U;
  reset_pages();
}

void PagedTextureRegionAllocator::reset_pages() {
  pages_.clear();
  pages_.resize(config_.max_pages);
  for (Page& page : pages_) {
    page.free_rects.clear();
    page.free_rects.push_back({0U, 0U, config_.page_width, config_.page_height});
  }
}

std::optional<PagedTextureRegionAllocator::FreeRect>
PagedTextureRegionAllocator::allocate_from_page(
    Page& page,
    u16 width,
    u16 height) {
  for (usize index = 0; index < page.free_rects.size(); ++index) {
    const FreeRect free = page.free_rects[index];
    if (width > free.width || height > free.height) {
      continue;
    }

    page.free_rects.erase(page.free_rects.begin() + static_cast<std::ptrdiff_t>(index));
    if (free.width > width) {
      page.free_rects.push_back({
          static_cast<u16>(free.x + width),
          free.y,
          static_cast<u16>(free.width - width),
          height});
    }
    if (free.height > height) {
      page.free_rects.push_back({
          free.x,
          static_cast<u16>(free.y + height),
          free.width,
          static_cast<u16>(free.height - height)});
    }
    return FreeRect{free.x, free.y, width, height};
  }
  return std::nullopt;
}

TextureRegionHandle PagedTextureRegionAllocator::publish_region(AtlasRegion region) {
  u32 id = 0U;
  if (!free_slot_ids_.empty()) {
    id = free_slot_ids_.back();
    free_slot_ids_.pop_back();
  } else {
    id = static_cast<u32>(slots_.size());
    slots_.push_back({});
    slots_[id].generation = generation_;
  }

  RegionSlot& slot = slots_[id];
  slot.region = region;
  slot.occupied = true;
  return {id, slot.generation};
}

std::optional<TextureRegionHandle> PagedTextureRegionAllocator::allocate(
    u16 width,
    u16 height) {
  if (width == 0U ||
      height == 0U ||
      width > config_.page_width ||
      height > config_.page_height) {
    return std::nullopt;
  }

  for (u16 page_index = 0U; page_index < pages_.size(); ++page_index) {
    std::optional<FreeRect> rect = allocate_from_page(pages_[page_index], width, height);
    if (!rect.has_value()) {
      continue;
    }

    allocated_pixels_ += static_cast<u64>(width) * height;
    return publish_region({
        page_index,
        rect->x,
        rect->y,
        rect->width,
        rect->height});
  }
  return std::nullopt;
}

bool PagedTextureRegionAllocator::validate_handle(
    TextureRegionHandle handle) const noexcept {
  return handle.id != 0U &&
         handle.id < slots_.size() &&
         slots_[handle.id].occupied &&
         slots_[handle.id].generation == handle.generation;
}

void PagedTextureRegionAllocator::free(TextureRegionHandle handle) {
  if (!validate_handle(handle)) {
    return;
  }

  RegionSlot& slot = slots_[handle.id];
  const AtlasRegion region = slot.region;
  if (region.page_index < pages_.size()) {
    pages_[region.page_index].free_rects.push_back({
        region.x,
        region.y,
        region.width,
        region.height});
  }
  allocated_pixels_ -= std::min<u64>(
      allocated_pixels_,
      static_cast<u64>(region.width) * region.height);
  slot.region = {};
  slot.occupied = false;
  slot.generation = next_generation(slot.generation);
  free_slot_ids_.push_back(handle.id);
}

std::optional<AtlasRegion> PagedTextureRegionAllocator::resolve(
    TextureRegionHandle handle) const noexcept {
  if (!validate_handle(handle)) {
    return std::nullopt;
  }
  return slots_[handle.id].region;
}

f32 PagedTextureRegionAllocator::usage_ratio() const noexcept {
  const u64 capacity = capacity_pixels();
  if (capacity == 0U) {
    return 0.0F;
  }
  return static_cast<f32>(allocated_pixels_) / static_cast<f32>(capacity);
}

} // namespace arv::render
