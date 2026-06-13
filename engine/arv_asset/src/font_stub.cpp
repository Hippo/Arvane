#include "arv/asset/font.hpp"

#include <algorithm>
#include <memory>
#include <utility>

namespace arv::asset {
namespace {

[[nodiscard]] usize approximate_glyph_count(std::string_view utf8) noexcept {
  usize count = 0;
  for (usize index = 0; index < utf8.size();) {
    const auto byte = static_cast<u8>(utf8[index]);
    usize advance = 1;
    if ((byte & 0xE0U) == 0xC0U) {
      advance = 2;
    } else if ((byte & 0xF0U) == 0xE0U) {
      advance = 3;
    } else if ((byte & 0xF8U) == 0xF0U) {
      advance = 4;
    }
    index += std::min(advance, utf8.size() - index);
    ++count;
  }
  return count;
}

} // namespace

struct FontSystem::Impl {
  struct FontFaceRecord {
    AssetID asset_id{};
    u32 pixel_size{16U};
  };

  std::vector<FontFaceRecord> faces{{}};
  Image atlas{};
  bool atlas_dirty_flag{};

  Impl() {
    atlas.width = 1;
    atlas.height = 1;
    atlas.channels = 4;
    atlas.pixels = {255U, 255U, 255U, 0U};
  }

  [[nodiscard]] const FontFaceRecord* face(FontFaceHandle handle) const noexcept {
    if (!handle.valid() || handle.id.value >= faces.size()) {
      return nullptr;
    }
    return &faces[handle.id.value];
  }

  [[nodiscard]] FontMetrics metrics(FontFaceHandle handle) const noexcept {
    const FontFaceRecord* record = face(handle);
    const f32 size =
        record == nullptr ? 16.0F : static_cast<f32>(std::max(record->pixel_size, 1U));
    return {
        size * 0.78F,
        -size * 0.22F,
        size * 1.18F};
  }
};

FontSystem::FontSystem() : impl_(std::make_unique<Impl>()) {
}

FontSystem::FontSystem(FontSystem&&) noexcept = default;

FontSystem& FontSystem::operator=(FontSystem&&) noexcept = default;

FontSystem::~FontSystem() = default;

bool FontSystem::initialized() const noexcept {
  return impl_ != nullptr;
}

FontFaceResult FontSystem::load_face(
    const std::filesystem::path& path,
    u32 pixel_size,
    AssetID asset_id) {
  (void)path;
  if (pixel_size == 0U) {
    return FontFaceResult::failure(
        {asset_id.value, path, "load font", "font pixel size must be positive"});
  }
  Impl::FontFaceRecord record;
  record.asset_id = std::move(asset_id);
  record.pixel_size = pixel_size;
  const FontFaceHandle handle{FontFaceID{static_cast<u32>(impl_->faces.size())}};
  impl_->faces.push_back(std::move(record));
  return FontFaceResult::success(handle);
}

FontStack FontSystem::make_stack(std::span<const FontFaceHandle> faces) const {
  FontStack stack;
  for (const FontFaceHandle face : faces) {
    if (impl_->face(face) != nullptr) {
      stack.faces.push_back(face);
    }
  }
  return stack;
}

std::optional<FontFaceHandle> FontSystem::select_font_for_codepoint(
    const FontStack& stack,
    char32_t codepoint) const {
  (void)codepoint;
  if (stack.faces.empty()) {
    return std::nullopt;
  }
  return stack.faces.front();
}

FontSpansResult FontSystem::fallback_spans(
    std::string_view utf8,
    const FontStack& stack) const {
  if (stack.faces.empty()) {
    return FontSpansResult::failure(
        {{}, {}, "resolve font fallback", "font stack is empty"});
  }
  if (utf8.empty()) {
    return FontSpansResult::success({});
  }
  return FontSpansResult::success(
      std::vector<FontSpan>{{stack.faces.front(), 0U, utf8.size()}});
}

TextMeasureResult FontSystem::measure_text(
    std::string_view utf8,
    const FontStack& stack,
    TextDirection direction) const {
  (void)direction;
  if (stack.faces.empty()) {
    return TextMeasureResult::failure(
        {{}, {}, "measure text", "font stack is empty"});
  }
  const FontMetrics base_metrics = metrics(stack.faces.front());
  f32 max_width = 0.0F;
  f32 line_width = 0.0F;
  u32 line_count = 1U;
  usize line_begin = 0U;
  while (line_begin <= utf8.size()) {
    const usize newline = utf8.find('\n', line_begin);
    const usize line_end =
        newline == std::string_view::npos ? utf8.size() : newline;
    line_width =
        static_cast<f32>(approximate_glyph_count(utf8.substr(line_begin, line_end - line_begin))) *
        base_metrics.line_height * 0.52F;
    max_width = std::max(max_width, line_width);
    if (newline == std::string_view::npos) {
      break;
    }
    ++line_count;
    line_begin = newline + 1U;
  }
  return TextMeasureResult::success({
      max_width,
      base_metrics.line_height * static_cast<f32>(line_count),
      base_metrics.ascender,
      base_metrics.descender,
      base_metrics.line_height});
}

TextLayoutResult FontSystem::layout_text(
    std::string_view utf8,
    const FontStack& stack,
    Vec2f origin,
    Vec4f color,
    TextDirection direction) {
  (void)origin;
  (void)color;
  TextLayout layout;
  auto measure = measure_text(utf8, stack, direction);
  if (!measure) {
    return TextLayoutResult::failure(measure.error());
  }
  layout.measure = measure.value();
  return TextLayoutResult::success(std::move(layout));
}

FontMetrics FontSystem::metrics(FontFaceHandle face) const noexcept {
  return impl_ == nullptr ? FontMetrics{} : impl_->metrics(face);
}

usize FontSystem::cached_glyph_count(FontFaceHandle face) const noexcept {
  (void)face;
  return 0U;
}

const Image& FontSystem::atlas_image() const noexcept {
  return impl_->atlas;
}

bool FontSystem::atlas_dirty() const noexcept {
  return impl_ != nullptr && impl_->atlas_dirty_flag;
}

void FontSystem::clear_atlas_dirty() noexcept {
  if (impl_ != nullptr) {
    impl_->atlas_dirty_flag = false;
  }
}

} // namespace arv::asset
