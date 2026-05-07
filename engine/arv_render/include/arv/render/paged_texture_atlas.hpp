#pragma once

#include "arv/render/render_types.hpp"

#include <glad/glad.h>

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arv::render {

class PagedTextureAtlas {
public:
  struct Config {
    u16 page_width{2048};
    u16 page_height{2048};
    u16 max_pages{1};
    TextureFormat format{TextureFormat::Rgba8};
    bool generate_mipmaps{};
    std::string debug_name{};
  };

  PagedTextureAtlas();
  explicit PagedTextureAtlas(Config config);

  [[nodiscard]] bool initialize_on_render_thread();
  void destroy_on_render_thread();

  [[nodiscard]] std::optional<TextureRegionHandle> allocate(u16 width, u16 height);
  void free(TextureRegionHandle handle);

  [[nodiscard]] std::optional<AtlasRegion> resolve(TextureRegionHandle handle) const;

  [[nodiscard]] bool upload_rgba8_on_render_thread(
      TextureRegionHandle handle,
      std::span<const std::byte> rgba8_pixels,
      u16 width,
      u16 height);

  [[nodiscard]] GLuint gl_texture_array() const noexcept {
    return gl_texture_array_;
  }

  [[nodiscard]] u16 page_width() const noexcept {
    return actual_page_width_;
  }

  [[nodiscard]] u16 page_height() const noexcept {
    return actual_page_height_;
  }

  [[nodiscard]] u16 max_pages() const noexcept {
    return actual_max_pages_;
  }

  [[nodiscard]] u64 allocated_pixels() const noexcept {
    return allocated_pixels_;
  }

  [[nodiscard]] u64 capacity_pixels() const noexcept {
    return static_cast<u64>(actual_page_width_) * actual_page_height_ * actual_max_pages_;
  }

  [[nodiscard]] f32 usage_ratio() const noexcept;

private:
  struct Rect {
    u16 x{};
    u16 y{};
    u16 width{};
    u16 height{};
  };

  struct PageAllocator {
    std::vector<Rect> free_rects{};
  };

  struct RegionSlot {
    AtlasRegion region{};
    u32 generation{1};
    bool occupied{};
  };

  [[nodiscard]] std::optional<Rect> allocate_from_page(PageAllocator& page, u16 width, u16 height);
  [[nodiscard]] TextureRegionHandle publish_region(AtlasRegion region);
  [[nodiscard]] bool validate_handle(TextureRegionHandle handle) const noexcept;
  void reset_allocators();

  Config config_{};
  u16 actual_page_width_{};
  u16 actual_page_height_{};
  u16 actual_max_pages_{};
  GLuint gl_texture_array_{};
  bool initialized_{};
  std::vector<PageAllocator> pages_{};
  std::vector<RegionSlot> slots_{{}};
  std::vector<u32> free_slot_ids_{};
  u64 allocated_pixels_{};
};

struct PlayerSkinAtlasConfig {
  u16 page_size{2048};
  u16 max_pages{8};

  struct CleanupConfig {
    u32 interval_seconds{60};
    u32 evict_after_unused_seconds{300};
  };

  struct PressureCleanupConfig {
    bool enabled{true};
    f32 usage_threshold{0.85F};
    f32 target_usage{0.65F};
  };

  CleanupConfig cleanup{};
  PressureCleanupConfig pressure_cleanup{};
};

struct ResidentSkinTexture {
  std::string skin_hash{};
  TextureRegionHandle region{};
  u16 width{};
  u16 height{};
  u64 last_used_frame{};
  std::chrono::steady_clock::time_point last_used_at{};
  u64 pinned_frame{};
  u32 pin_count{};
};

class PlayerSkinAtlas {
public:
  explicit PlayerSkinAtlas(PlayerSkinAtlasConfig config = {});

  void configure(PlayerSkinAtlasConfig config);
  [[nodiscard]] bool initialize_on_render_thread();
  void destroy_on_render_thread();

  [[nodiscard]] std::optional<TextureRegionHandle> upload_skin_rgba8_on_render_thread(
      std::string skin_hash,
      std::span<const std::byte> rgba8_pixels,
      u16 width,
      u16 height,
      u64 frame_index,
      std::chrono::steady_clock::time_point now);

  [[nodiscard]] std::optional<AtlasRegion> resolve(TextureRegionHandle handle) const;
  [[nodiscard]] const ResidentSkinTexture* resident(std::string_view skin_hash) const noexcept;
  [[nodiscard]] const ResidentSkinTexture* resident(TextureRegionHandle handle) const noexcept;

  [[nodiscard]] bool mark_used(
      TextureRegionHandle handle,
      u64 frame_index,
      std::chrono::steady_clock::time_point now,
      bool pinned);

  void run_periodic_cleanup(u64 frame_index, std::chrono::steady_clock::time_point now);
  [[nodiscard]] usize evict_unused(
      u64 frame_index,
      std::chrono::steady_clock::time_point now,
      u32 unused_seconds);

  [[nodiscard]] GLuint gl_texture_array() const noexcept {
    return atlas_.gl_texture_array();
  }

  [[nodiscard]] u16 page_width() const noexcept {
    return atlas_.page_width();
  }

  [[nodiscard]] u16 page_height() const noexcept {
    return atlas_.page_height();
  }

  [[nodiscard]] f32 usage_ratio() const noexcept {
    return atlas_.usage_ratio();
  }

private:
  [[nodiscard]] bool is_evictable(
      const ResidentSkinTexture& resident,
      u64 frame_index) const noexcept;
  [[nodiscard]] usize evict_for_pressure(
      u64 frame_index,
      std::chrono::steady_clock::time_point now);
  void evict_at(usize index);

  PlayerSkinAtlasConfig config_{};
  PagedTextureAtlas atlas_;
  HashMap<std::string, usize> resident_by_hash_{};
  HashMap<u32, usize> resident_by_handle_id_{};
  std::vector<ResidentSkinTexture> residents_{};
  std::chrono::steady_clock::time_point last_cleanup_at_{};
};

} // namespace arv::render
