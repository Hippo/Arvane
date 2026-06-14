#include "arv/asset/font.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <utility>

#define STB_TRUETYPE_IMPLEMENTATION
#include "../../../vendors/imgui/imstb_truetype.h"

namespace arv::asset {
namespace {

constexpr usize MaxTextMeasureCacheEntries = 1024;
constexpr usize MaxTextLayoutCacheEntries = 1024;

struct DecodedCodepoint {
  char32_t codepoint{};
  usize byte_begin{};
  usize byte_end{};
};

[[nodiscard]] std::vector<DecodedCodepoint> decode_utf8(std::string_view utf8) {
  std::vector<DecodedCodepoint> decoded;
  for (usize index = 0; index < utf8.size();) {
    const usize begin = index;
    const auto byte = static_cast<u8>(utf8[index]);
    char32_t codepoint = 0xFFFDU;
    usize length = 1U;
    if (byte < 0x80U) {
      codepoint = byte;
    } else if ((byte & 0xE0U) == 0xC0U && index + 1U < utf8.size()) {
      codepoint = ((byte & 0x1FU) << 6U) |
                  (static_cast<u8>(utf8[index + 1U]) & 0x3FU);
      length = 2U;
    } else if ((byte & 0xF0U) == 0xE0U && index + 2U < utf8.size()) {
      codepoint = ((byte & 0x0FU) << 12U) |
                  ((static_cast<u8>(utf8[index + 1U]) & 0x3FU) << 6U) |
                  (static_cast<u8>(utf8[index + 2U]) & 0x3FU);
      length = 3U;
    } else if ((byte & 0xF8U) == 0xF0U && index + 3U < utf8.size()) {
      codepoint = ((byte & 0x07U) << 18U) |
                  ((static_cast<u8>(utf8[index + 1U]) & 0x3FU) << 12U) |
                  ((static_cast<u8>(utf8[index + 2U]) & 0x3FU) << 6U) |
                  (static_cast<u8>(utf8[index + 3U]) & 0x3FU);
      length = 4U;
    }
    decoded.push_back({codepoint, begin, begin + length});
    index += length;
  }
  return decoded;
}

[[nodiscard]] Expected<std::vector<u8>, AssetError> read_binary_file(
    const std::filesystem::path& path,
    const AssetID& asset_id) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    return Expected<std::vector<u8>, AssetError>::failure({
        asset_id.value,
        path,
        "read font",
        "could not open font file"});
  }
  const std::ifstream::pos_type end = input.tellg();
  if (end <= 0) {
    return Expected<std::vector<u8>, AssetError>::failure({
        asset_id.value,
        path,
        "read font",
        "font file was empty"});
  }
  std::vector<u8> bytes(static_cast<usize>(end));
  input.seekg(0, std::ios::beg);
  input.read(
      reinterpret_cast<char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  if (!input) {
    return Expected<std::vector<u8>, AssetError>::failure({
        asset_id.value,
        path,
        "read font",
        "could not read font file"});
  }
  return Expected<std::vector<u8>, AssetError>::success(std::move(bytes));
}

template<typename T>
void append_cache_bytes(std::string& key, const T& value) {
  const auto* bytes = reinterpret_cast<const char*>(&value);
  key.append(bytes, sizeof(T));
}

[[nodiscard]] u32 float_bits(f32 value) noexcept {
  u32 bits = 0U;
  static_assert(sizeof(bits) == sizeof(value));
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

[[nodiscard]] std::string make_text_cache_key(
    std::string_view utf8,
    const FontStack& stack,
    TextDirection direction) {
  std::string key;
  key.reserve(sizeof(u8) + sizeof(u32) * (stack.faces.size() + 2U) + utf8.size());
  append_cache_bytes(key, static_cast<u8>(direction));
  append_cache_bytes(key, static_cast<u32>(stack.faces.size()));
  for (const FontFaceHandle face : stack.faces) {
    append_cache_bytes(key, face.id.value);
  }
  append_cache_bytes(key, static_cast<u32>(utf8.size()));
  key.append(utf8.data(), utf8.size());
  return key;
}

[[nodiscard]] std::string make_layout_cache_key(
    std::string_view utf8,
    const FontStack& stack,
    Vec2f origin,
    Vec4f color,
    TextDirection direction) {
  std::string key = make_text_cache_key(utf8, stack, direction);
  append_cache_bytes(key, float_bits(origin.x));
  append_cache_bytes(key, float_bits(origin.y));
  append_cache_bytes(key, float_bits(color.x));
  append_cache_bytes(key, float_bits(color.y));
  append_cache_bytes(key, float_bits(color.z));
  append_cache_bytes(key, float_bits(color.w));
  return key;
}

} // namespace

struct FontSystem::Impl {
  struct GlyphInfo {
    u32 page{};
    Vec2i uv_min{};
    Vec2i uv_max{};
    Vec2i size{};
    Vec2i offset{};
  };

  struct FontFaceRecord {
    AssetID asset_id{};
    std::filesystem::path path{};
    std::vector<u8> data{};
    stbtt_fontinfo font{};
    bool loaded{};
    u32 pixel_size{16U};
    f32 scale{1.0F};
    i32 ascent{};
    i32 descent{};
    i32 line_gap{};
    HashMap<u32, GlyphInfo> glyphs{};
  };

  std::vector<FontFaceRecord> faces{{}};
  Image atlas{};
  u32 atlas_padding{1U};
  u32 atlas_cursor_x{1U};
  u32 atlas_cursor_y{1U};
  u32 atlas_row_height{};
  bool atlas_dirty_flag{};
  HashMap<std::string, TextMeasure> measure_cache{};
  HashMap<std::string, TextLayout> layout_cache{};

  Impl() {
    atlas.width = 1024U;
    atlas.height = 1024U;
    atlas.channels = 4U;
    atlas.pixels.assign(static_cast<usize>(atlas.width) * atlas.height * 4U, 0U);
  }

  [[nodiscard]] const FontFaceRecord* face(FontFaceHandle handle) const noexcept {
    if (!handle.valid() || handle.id.value >= faces.size()) {
      return nullptr;
    }
    return &faces[handle.id.value];
  }

  [[nodiscard]] FontFaceRecord* face(FontFaceHandle handle) noexcept {
    if (!handle.valid() || handle.id.value >= faces.size()) {
      return nullptr;
    }
    return &faces[handle.id.value];
  }

  [[nodiscard]] FontMetrics metrics(FontFaceHandle handle) const noexcept {
    const FontFaceRecord* record = face(handle);
    if (record == nullptr || !record->loaded) {
      const f32 size =
          record == nullptr ? 16.0F : static_cast<f32>(std::max(record->pixel_size, 1U));
      return {size * 0.78F, -size * 0.22F, size * 1.18F};
    }
    const f32 ascender = static_cast<f32>(record->ascent) * record->scale;
    const f32 descender = static_cast<f32>(record->descent) * record->scale;
    const f32 line_gap = static_cast<f32>(record->line_gap) * record->scale;
    return {ascender, descender, ascender - descender + line_gap};
  }

  void clear_text_caches() {
    measure_cache.clear();
    layout_cache.clear();
  }

  [[nodiscard]] f32 advance_for(FontFaceRecord& record, char32_t codepoint, char32_t previous) {
    if (codepoint == U'\t') {
      return advance_for(record, U' ', 0U) * 4.0F;
    }
    i32 advance = 0;
    i32 left_bearing = 0;
    stbtt_GetCodepointHMetrics(
        &record.font,
        static_cast<int>(codepoint),
        &advance,
        &left_bearing);
    f32 result = static_cast<f32>(advance) * record.scale;
    if (previous != 0U) {
      result += static_cast<f32>(
                    stbtt_GetCodepointKernAdvance(
                        &record.font,
                        static_cast<int>(previous),
                        static_cast<int>(codepoint))) *
                record.scale;
    }
    return result;
  }

  [[nodiscard]] Expected<GlyphInfo, AssetError> ensure_glyph(
      FontFaceHandle handle,
      char32_t codepoint) {
    FontFaceRecord* record = face(handle);
    if (record == nullptr || !record->loaded) {
      return Expected<GlyphInfo, AssetError>::failure({
          {},
          {},
          "cache glyph",
          "invalid font face handle"});
    }
    const auto found = record->glyphs.find(static_cast<u32>(codepoint));
    if (found != record->glyphs.end()) {
      return Expected<GlyphInfo, AssetError>::success(found->second);
    }

    if (codepoint == U'\t') {
      codepoint = U' ';
    }

    i32 width = 0;
    i32 height = 0;
    i32 x_offset = 0;
    i32 y_offset = 0;
    unsigned char* bitmap = stbtt_GetCodepointBitmap(
        &record->font,
        0.0F,
        record->scale,
        static_cast<int>(codepoint),
        &width,
        &height,
        &x_offset,
        &y_offset);
    if (width < 0 || height < 0) {
      return Expected<GlyphInfo, AssetError>::failure({
          record->asset_id.value,
          record->path,
          "rasterize glyph",
          "stb_truetype returned an invalid glyph size"});
    }

    const u32 glyph_width = static_cast<u32>(width);
    const u32 glyph_height = static_cast<u32>(height);
    const u32 padded_width = glyph_width + atlas_padding * 2U;
    const u32 padded_height = glyph_height + atlas_padding * 2U;
    if (atlas_cursor_x + padded_width >= atlas.width) {
      atlas_cursor_x = atlas_padding;
      atlas_cursor_y += atlas_row_height + atlas_padding;
      atlas_row_height = 0U;
    }
    if (atlas_cursor_y + padded_height >= atlas.height) {
      if (bitmap != nullptr) {
        stbtt_FreeBitmap(bitmap, nullptr);
      }
      return Expected<GlyphInfo, AssetError>::failure({
          record->asset_id.value,
          record->path,
          "cache glyph",
          "font atlas page is full"});
    }

    const u32 dst_x = atlas_cursor_x + atlas_padding;
    const u32 dst_y = atlas_cursor_y + atlas_padding;
    for (u32 y = 0; y < glyph_height; ++y) {
      for (u32 x = 0; x < glyph_width; ++x) {
        const usize dst =
            (static_cast<usize>(dst_y + y) * atlas.width + (dst_x + x)) * 4U;
        const usize src = static_cast<usize>(y) * glyph_width + x;
        const u8 coverage = bitmap == nullptr ? 0U : bitmap[src];
        atlas.pixels[dst + 0U] = 255U;
        atlas.pixels[dst + 1U] = 255U;
        atlas.pixels[dst + 2U] = 255U;
        atlas.pixels[dst + 3U] = coverage;
      }
    }
    if (bitmap != nullptr) {
      stbtt_FreeBitmap(bitmap, nullptr);
    }

    GlyphInfo info;
    info.page = 0U;
    info.uv_min = {static_cast<i32>(dst_x), static_cast<i32>(dst_y)};
    info.uv_max = {
        static_cast<i32>(dst_x + glyph_width),
        static_cast<i32>(dst_y + glyph_height)};
    info.size = {static_cast<i32>(glyph_width), static_cast<i32>(glyph_height)};
    info.offset = {x_offset, y_offset};
    record->glyphs.emplace(static_cast<u32>(codepoint), info);
    atlas_cursor_x += padded_width + atlas_padding;
    atlas_row_height = std::max(atlas_row_height, padded_height);
    atlas_dirty_flag = true;
    return Expected<GlyphInfo, AssetError>::success(info);
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
  if (pixel_size == 0U) {
    return FontFaceResult::failure({
        asset_id.value,
        path,
        "load font",
        "font pixel size must be positive"});
  }

  auto bytes = read_binary_file(path, asset_id);
  if (!bytes) {
    return FontFaceResult::failure(bytes.error());
  }

  Impl::FontFaceRecord record;
  record.asset_id = std::move(asset_id);
  record.path = path;
  record.data = std::move(bytes.value());
  record.pixel_size = pixel_size;
  const int offset = stbtt_GetFontOffsetForIndex(record.data.data(), 0);
  if (offset < 0 ||
      !stbtt_InitFont(&record.font, record.data.data(), offset)) {
    return FontFaceResult::failure({
        record.asset_id.value,
        path,
        "load font",
        "stb_truetype could not load face"});
  }
  record.loaded = true;
  record.scale =
      stbtt_ScaleForPixelHeight(&record.font, static_cast<float>(pixel_size));
  stbtt_GetFontVMetrics(
      &record.font,
      &record.ascent,
      &record.descent,
      &record.line_gap);

  const FontFaceHandle handle{FontFaceID{static_cast<u32>(impl_->faces.size())}};
  impl_->faces.push_back(std::move(record));
  impl_->clear_text_caches();
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
  if (stack.faces.empty()) {
    return std::nullopt;
  }
  for (const FontFaceHandle handle : stack.faces) {
    const Impl::FontFaceRecord* record = impl_->face(handle);
    if (record != nullptr && record->loaded &&
        stbtt_FindGlyphIndex(&record->font, static_cast<int>(codepoint)) != 0) {
      return handle;
    }
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
  std::vector<FontSpan> spans;
  for (const DecodedCodepoint& decoded : decode_utf8(utf8)) {
    auto selected = select_font_for_codepoint(stack, decoded.codepoint);
    if (!selected) {
      return FontSpansResult::failure(
          {{}, {}, "resolve font fallback", "no usable fonts in stack"});
    }
    if (!spans.empty() && spans.back().face == selected.value() &&
        spans.back().byte_end == decoded.byte_begin) {
      spans.back().byte_end = decoded.byte_end;
    } else {
      spans.push_back({selected.value(), decoded.byte_begin, decoded.byte_end});
    }
  }
  return FontSpansResult::success(std::move(spans));
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
  std::string cache_key = make_text_cache_key(utf8, stack, direction);
  if (const auto found = impl_->measure_cache.find(cache_key);
      found != impl_->measure_cache.end()) {
    return TextMeasureResult::success(found->second);
  }

  const FontMetrics base_metrics = metrics(stack.faces.front());
  f32 max_width = 0.0F;
  u32 line_count = 1U;
  for (usize line_begin = 0U; line_begin <= utf8.size();) {
    const usize newline = utf8.find('\n', line_begin);
    const usize line_end = newline == std::string_view::npos ? utf8.size() : newline;
    const std::string_view line = utf8.substr(line_begin, line_end - line_begin);
    f32 line_width = 0.0F;
    char32_t previous = 0U;
    for (const DecodedCodepoint& decoded : decode_utf8(line)) {
      const auto face = select_font_for_codepoint(stack, decoded.codepoint);
      Impl::FontFaceRecord* record =
          face.has_value() ? impl_->face(face.value()) : nullptr;
      if (record == nullptr || !record->loaded) {
        continue;
      }
      line_width += impl_->advance_for(*record, decoded.codepoint, previous);
      previous = decoded.codepoint;
    }
    max_width = std::max(max_width, line_width);
    if (newline == std::string_view::npos) {
      break;
    }
    ++line_count;
    line_begin = line_end + 1U;
  }

  TextMeasure result{
      max_width,
      base_metrics.line_height * static_cast<f32>(line_count),
      base_metrics.ascender,
      base_metrics.descender,
      base_metrics.line_height};
  if (impl_->measure_cache.size() >= MaxTextMeasureCacheEntries) {
    impl_->measure_cache.clear();
  }
  impl_->measure_cache.emplace(std::move(cache_key), result);
  return TextMeasureResult::success(result);
}

TextLayoutResult FontSystem::layout_text(
    std::string_view utf8,
    const FontStack& stack,
    Vec2f origin,
    Vec4f color,
    TextDirection direction) {
  (void)direction;
  if (stack.faces.empty()) {
    return TextLayoutResult::failure(
        {{}, {}, "layout text", "font stack is empty"});
  }
  std::string cache_key = make_layout_cache_key(utf8, stack, origin, color, direction);
  if (const auto found = impl_->layout_cache.find(cache_key);
      found != impl_->layout_cache.end()) {
    return TextLayoutResult::success(found->second);
  }

  const FontMetrics base_metrics = metrics(stack.faces.front());
  TextLayout layout;
  f32 baseline_y = origin.y + base_metrics.ascender;
  u32 line_count = 1U;
  f32 max_width = 0.0F;
  for (usize line_begin = 0U; line_begin <= utf8.size();) {
    const usize newline = utf8.find('\n', line_begin);
    const usize line_end = newline == std::string_view::npos ? utf8.size() : newline;
    const std::string_view line = utf8.substr(line_begin, line_end - line_begin);
    f32 pen_x = origin.x;
    char32_t previous = 0U;
    for (const DecodedCodepoint& decoded : decode_utf8(line)) {
      const auto face = select_font_for_codepoint(stack, decoded.codepoint);
      Impl::FontFaceRecord* record =
          face.has_value() ? impl_->face(face.value()) : nullptr;
      if (record == nullptr || !record->loaded) {
        continue;
      }
      auto glyph = impl_->ensure_glyph(face.value(), decoded.codepoint);
      if (!glyph) {
        return TextLayoutResult::failure(glyph.error());
      }
      const Impl::GlyphInfo info = glyph.value();
      if (info.size.x > 0 && info.size.y > 0) {
        const Vec2f glyph_min{
            pen_x + static_cast<f32>(info.offset.x),
            baseline_y + static_cast<f32>(info.offset.y)};
        const Vec2f glyph_max{
            glyph_min.x + static_cast<f32>(info.size.x),
            glyph_min.y + static_cast<f32>(info.size.y)};
        layout.quads.push_back({
            glyph_min,
            glyph_max,
            {
                static_cast<f32>(info.uv_min.x) / static_cast<f32>(impl_->atlas.width),
                static_cast<f32>(info.uv_min.y) / static_cast<f32>(impl_->atlas.height),
            },
            {
                static_cast<f32>(info.uv_max.x) / static_cast<f32>(impl_->atlas.width),
                static_cast<f32>(info.uv_max.y) / static_cast<f32>(impl_->atlas.height),
            },
            color,
            info.page});
      }
      pen_x += impl_->advance_for(*record, decoded.codepoint, previous);
      previous = decoded.codepoint;
    }
    max_width = std::max(max_width, pen_x - origin.x);
    if (newline == std::string_view::npos) {
      break;
    }
    ++line_count;
    baseline_y += base_metrics.line_height;
    line_begin = line_end + 1U;
  }

  layout.measure = {
      max_width,
      base_metrics.line_height * static_cast<f32>(line_count),
      base_metrics.ascender,
      base_metrics.descender,
      base_metrics.line_height};
  if (impl_->layout_cache.size() >= MaxTextLayoutCacheEntries) {
    impl_->layout_cache.clear();
  }
  impl_->layout_cache.emplace(std::move(cache_key), layout);
  return TextLayoutResult::success(std::move(layout));
}

FontMetrics FontSystem::metrics(FontFaceHandle face) const noexcept {
  return impl_ == nullptr ? FontMetrics{} : impl_->metrics(face);
}

usize FontSystem::cached_glyph_count(FontFaceHandle face) const noexcept {
  const Impl::FontFaceRecord* record = impl_ == nullptr ? nullptr : impl_->face(face);
  return record == nullptr ? 0U : record->glyphs.size();
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
