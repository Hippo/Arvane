#include "arv/render/paged_texture_atlas.hpp"

#include "arv/core/log.hpp"

#include "render_gl_utils.hpp"

#include <algorithm>
#include <utility>

namespace arv::render {
namespace {

[[nodiscard]] GLint gl_internal_format(TextureFormat format) noexcept {
  switch (format) {
    case TextureFormat::Rgba8:
      return GL_RGBA8;
  }
  return GL_RGBA8;
}

[[nodiscard]] GLenum gl_upload_format(TextureFormat format) noexcept {
  switch (format) {
    case TextureFormat::Rgba8:
      return GL_RGBA;
  }
  return GL_RGBA;
}

[[nodiscard]] GLenum gl_upload_type(TextureFormat format) noexcept {
  switch (format) {
    case TextureFormat::Rgba8:
      return GL_UNSIGNED_BYTE;
  }
  return GL_UNSIGNED_BYTE;
}

[[nodiscard]] u16 clamp_positive_u16(u16 value, u16 fallback) noexcept {
  return value == 0U ? fallback : value;
}

[[nodiscard]] u16 clamped_to_gl_limit(u16 value, GLint limit) noexcept {
  if (limit <= 0) {
    return value;
  }
  return static_cast<u16>(std::min<u32>(value, static_cast<u32>(limit)));
}

} // namespace

PagedTextureAtlas::PagedTextureAtlas() : PagedTextureAtlas(Config{}) {
}

PagedTextureAtlas::PagedTextureAtlas(Config config)
    : config_(std::move(config)),
      actual_page_width_(clamp_positive_u16(config_.page_width, 1U)),
      actual_page_height_(clamp_positive_u16(config_.page_height, 1U)),
      actual_max_pages_(clamp_positive_u16(config_.max_pages, 1U)) {
  reset_allocators();
}

void PagedTextureAtlas::reset_allocators() {
  allocator_.configure({actual_page_width_, actual_page_height_, actual_max_pages_});
}

bool PagedTextureAtlas::initialize_on_render_thread() {
  if (initialized_) {
    return true;
  }

  GLint max_texture_size = 0;
  GLint max_array_layers = 0;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
  glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, &max_array_layers);

  actual_page_width_ = clamped_to_gl_limit(clamp_positive_u16(config_.page_width, 1U), max_texture_size);
  actual_page_height_ = clamped_to_gl_limit(clamp_positive_u16(config_.page_height, 1U), max_texture_size);
  actual_max_pages_ = clamped_to_gl_limit(clamp_positive_u16(config_.max_pages, 1U), max_array_layers);
  if (actual_page_width_ == 0U || actual_page_height_ == 0U || actual_max_pages_ == 0U) {
    core::warn("render", "paged atlas {} has no valid GL capacity", config_.debug_name);
    return false;
  }
  reset_allocators();

  glGenTextures(1, &gl_texture_array_);
  glBindTexture(GL_TEXTURE_2D_ARRAY, gl_texture_array_);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(
      GL_TEXTURE_2D_ARRAY,
      GL_TEXTURE_MIN_FILTER,
      config_.generate_mipmaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST);
  glTexParameteri(
      GL_TEXTURE_2D_ARRAY,
      GL_TEXTURE_MAX_LEVEL,
      config_.generate_mipmaps
          ? static_cast<GLint>(detail::full_mipmap_max_level(actual_page_width_, actual_page_height_))
          : 0);
  glTexImage3D(
      GL_TEXTURE_2D_ARRAY,
      0,
      gl_internal_format(config_.format),
      static_cast<GLsizei>(actual_page_width_),
      static_cast<GLsizei>(actual_page_height_),
      static_cast<GLsizei>(actual_max_pages_),
      0,
      gl_upload_format(config_.format),
      gl_upload_type(config_.format),
      nullptr);
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

  initialized_ = gl_texture_array_ != 0;
  if (initialized_) {
    core::info(
        "render",
        "paged atlas {} initialized as {}x{}x{} texture array",
        config_.debug_name,
        actual_page_width_,
        actual_page_height_,
        actual_max_pages_);
  }
  return initialized_;
}

void PagedTextureAtlas::destroy_on_render_thread() {
  if (gl_texture_array_ != 0) {
    glDeleteTextures(1, &gl_texture_array_);
  }
  gl_texture_array_ = 0;
  initialized_ = false;
  reset_allocators();
}

std::optional<TextureRegionHandle> PagedTextureAtlas::allocate(u16 width, u16 height) {
  return allocator_.allocate(width, height);
}

void PagedTextureAtlas::free(TextureRegionHandle handle) {
  allocator_.free(handle);
}

std::optional<AtlasRegion> PagedTextureAtlas::resolve(TextureRegionHandle handle) const {
  return allocator_.resolve(handle);
}

bool PagedTextureAtlas::upload_rgba8_on_render_thread(
    TextureRegionHandle handle,
    std::span<const std::byte> rgba8_pixels,
    u16 width,
    u16 height) {
  if (config_.format != TextureFormat::Rgba8 || gl_texture_array_ == 0) {
    return false;
  }
  const std::optional<AtlasRegion> region = resolve(handle);
  if (!region.has_value() || region->width != width || region->height != height) {
    return false;
  }
  if (rgba8_pixels.size() != static_cast<usize>(width) * height * 4U) {
    return false;
  }

  glBindTexture(GL_TEXTURE_2D_ARRAY, gl_texture_array_);
  glTexSubImage3D(
      GL_TEXTURE_2D_ARRAY,
      0,
      static_cast<GLint>(region->x),
      static_cast<GLint>(region->y),
      static_cast<GLint>(region->page_index),
      static_cast<GLsizei>(width),
      static_cast<GLsizei>(height),
      1,
      GL_RGBA,
      GL_UNSIGNED_BYTE,
      rgba8_pixels.data());
  if (config_.generate_mipmaps) {
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
  }
  glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
  return true;
}

f32 PagedTextureAtlas::usage_ratio() const noexcept {
  return allocator_.usage_ratio();
}

PlayerSkinAtlas::PlayerSkinAtlas(PlayerSkinAtlasConfig config)
    : config_(config),
      atlas_({
          config.page_size,
          config.page_size,
          config.max_pages,
          TextureFormat::Rgba8,
          false,
          "player_skins"}) {
}

void PlayerSkinAtlas::configure(PlayerSkinAtlasConfig config) {
  atlas_.destroy_on_render_thread();
  config_ = config;
  atlas_ = PagedTextureAtlas({
      config.page_size,
      config.page_size,
      config.max_pages,
      TextureFormat::Rgba8,
      false,
      "player_skins"});
  resident_by_hash_.clear();
  resident_by_handle_id_.clear();
  residents_.clear();
  last_cleanup_at_ = {};
}

bool PlayerSkinAtlas::initialize_on_render_thread() {
  return atlas_.initialize_on_render_thread();
}

void PlayerSkinAtlas::destroy_on_render_thread() {
  resident_by_hash_.clear();
  resident_by_handle_id_.clear();
  residents_.clear();
  last_cleanup_at_ = {};
  atlas_.destroy_on_render_thread();
}

const ResidentSkinTexture* PlayerSkinAtlas::resident(std::string_view skin_hash) const noexcept {
  const auto found = resident_by_hash_.find(std::string{skin_hash});
  if (found == resident_by_hash_.end() || found->second >= residents_.size()) {
    return nullptr;
  }
  return &residents_[found->second];
}

const ResidentSkinTexture* PlayerSkinAtlas::resident(TextureRegionHandle handle) const noexcept {
  const auto found = resident_by_handle_id_.find(handle.id);
  if (found == resident_by_handle_id_.end() || found->second >= residents_.size()) {
    return nullptr;
  }
  const ResidentSkinTexture& texture = residents_[found->second];
  return texture.region == handle ? &texture : nullptr;
}

std::optional<AtlasRegion> PlayerSkinAtlas::resolve(TextureRegionHandle handle) const {
  return atlas_.resolve(handle);
}

bool PlayerSkinAtlas::mark_used(
    TextureRegionHandle handle,
    u64 frame_index,
    std::chrono::steady_clock::time_point now,
    bool pinned) {
  const auto found = resident_by_handle_id_.find(handle.id);
  if (found == resident_by_handle_id_.end() || found->second >= residents_.size()) {
    return false;
  }
  ResidentSkinTexture& texture = residents_[found->second];
  if (!(texture.region == handle)) {
    return false;
  }
  texture.last_used_frame = frame_index;
  texture.last_used_at = now;
  if (pinned) {
    texture.pinned_frame = frame_index;
  }
  return true;
}

bool PlayerSkinAtlas::is_evictable(
    const ResidentSkinTexture& resident,
    u64 frame_index) const noexcept {
  return resident.pin_count == 0U && resident.pinned_frame != frame_index;
}

void PlayerSkinAtlas::evict_at(usize index) {
  if (index >= residents_.size()) {
    return;
  }

  atlas_.free(residents_[index].region);
  resident_by_hash_.erase(residents_[index].skin_hash);
  resident_by_handle_id_.erase(residents_[index].region.id);

  if (index + 1U != residents_.size()) {
    residents_[index] = std::move(residents_.back());
    resident_by_hash_[residents_[index].skin_hash] = index;
    resident_by_handle_id_[residents_[index].region.id] = index;
  }
  residents_.pop_back();
}

usize PlayerSkinAtlas::evict_for_pressure(
    u64 frame_index,
    std::chrono::steady_clock::time_point) {
  if (!config_.pressure_cleanup.enabled) {
    return 0U;
  }

  usize evicted = 0;
  while (atlas_.usage_ratio() > config_.pressure_cleanup.target_usage) {
    auto candidate = residents_.end();
    for (auto it = residents_.begin(); it != residents_.end(); ++it) {
      if (!is_evictable(*it, frame_index)) {
        continue;
      }
      if (candidate == residents_.end() ||
          it->last_used_frame < candidate->last_used_frame) {
        candidate = it;
      }
    }
    if (candidate == residents_.end()) {
      break;
    }
    const usize index = static_cast<usize>(std::distance(residents_.begin(), candidate));
    evict_at(index);
    ++evicted;
  }
  return evicted;
}

std::optional<TextureRegionHandle> PlayerSkinAtlas::upload_skin_rgba8_on_render_thread(
    std::string skin_hash,
    std::span<const std::byte> rgba8_pixels,
    u16 width,
    u16 height,
    u64 frame_index,
    std::chrono::steady_clock::time_point now) {
  if (skin_hash.empty() || width == 0U || height == 0U) {
    return std::nullopt;
  }
  if (const ResidentSkinTexture* existing = resident(skin_hash);
      existing != nullptr && existing->width == width && existing->height == height) {
    (void)mark_used(existing->region, frame_index, now, true);
    return existing->region;
  }

  if (atlas_.gl_texture_array() == 0 && !initialize_on_render_thread()) {
    return std::nullopt;
  }

  std::optional<TextureRegionHandle> handle = atlas_.allocate(width, height);
  if (!handle.has_value() ||
      atlas_.usage_ratio() > config_.pressure_cleanup.usage_threshold) {
    (void)evict_for_pressure(frame_index, now);
    if (!handle.has_value()) {
      handle = atlas_.allocate(width, height);
    }
  }
  if (!handle.has_value()) {
    core::warn("render", "player skin atlas could not allocate {}x{} skin {}", width, height, skin_hash);
    return std::nullopt;
  }
  if (!atlas_.upload_rgba8_on_render_thread(*handle, rgba8_pixels, width, height)) {
    atlas_.free(*handle);
    core::warn("render", "player skin atlas upload failed for {}", skin_hash);
    return std::nullopt;
  }

  ResidentSkinTexture resident;
  resident.skin_hash = std::move(skin_hash);
  resident.region = *handle;
  resident.width = width;
  resident.height = height;
  resident.last_used_frame = frame_index;
  resident.last_used_at = now;
  resident.pinned_frame = frame_index;
  const usize index = residents_.size();
  residents_.push_back(std::move(resident));
  resident_by_hash_[residents_[index].skin_hash] = index;
  resident_by_handle_id_[residents_[index].region.id] = index;
  return *handle;
}

void PlayerSkinAtlas::run_periodic_cleanup(
    u64 frame_index,
    std::chrono::steady_clock::time_point now) {
  if (last_cleanup_at_.time_since_epoch().count() != 0) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_cleanup_at_);
    if (elapsed.count() < config_.cleanup.interval_seconds) {
      return;
    }
  }
  last_cleanup_at_ = now;
  (void)evict_unused(frame_index, now, config_.cleanup.evict_after_unused_seconds);
}

usize PlayerSkinAtlas::evict_unused(
    u64 frame_index,
    std::chrono::steady_clock::time_point now,
    u32 unused_seconds) {
  usize evicted = 0;
  for (usize index = 0; index < residents_.size();) {
    const ResidentSkinTexture& texture = residents_[index];
    const auto unused_for = std::chrono::duration_cast<std::chrono::seconds>(now - texture.last_used_at);
    if (is_evictable(texture, frame_index) &&
        unused_for.count() >= static_cast<i64>(unused_seconds)) {
      evict_at(index);
      ++evicted;
      continue;
    }
    ++index;
  }
  return evicted;
}

} // namespace arv::render
