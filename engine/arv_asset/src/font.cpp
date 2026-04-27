#include "arv/asset/font.hpp"

#include <algorithm>
#include <fstream>
#include <memory>
#include <unordered_map>
#include <utility>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#include <hb-ft.h>
#include <hb.h>

namespace arv::asset {
namespace {

struct DecodedCodepoint {
  char32_t codepoint{};
  usize byte_begin{};
  usize byte_end{};
};

[[nodiscard]] Expected<std::vector<u8>, AssetError> read_binary_file(
    const std::filesystem::path& path,
    const AssetID& asset_id) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return Expected<std::vector<u8>, AssetError>::failure({
        asset_id.value,
        path,
        "read font",
        "file could not be opened"});
  }
  file.seekg(0, std::ios::end);
  std::vector<u8> bytes(static_cast<usize>(file.tellg()));
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file && !file.eof()) {
    return Expected<std::vector<u8>, AssetError>::failure({
        asset_id.value,
        path,
        "read font",
        "file read failed"});
  }
  return Expected<std::vector<u8>, AssetError>::success(std::move(bytes));
}

[[nodiscard]] std::vector<DecodedCodepoint> decode_utf8(std::string_view utf8) {
  std::vector<DecodedCodepoint> decoded;
  for (usize i = 0; i < utf8.size();) {
    const usize begin = i;
    const auto byte = static_cast<u8>(utf8[i]);
    char32_t codepoint = 0xFFFDU;
    usize length = 1;
    if (byte < 0x80U) {
      codepoint = byte;
    } else if ((byte & 0xE0U) == 0xC0U && i + 1U < utf8.size()) {
      codepoint = ((byte & 0x1FU) << 6U) | (static_cast<u8>(utf8[i + 1U]) & 0x3FU);
      length = 2;
    } else if ((byte & 0xF0U) == 0xE0U && i + 2U < utf8.size()) {
      codepoint = ((byte & 0x0FU) << 12U) |
                  ((static_cast<u8>(utf8[i + 1U]) & 0x3FU) << 6U) |
                  (static_cast<u8>(utf8[i + 2U]) & 0x3FU);
      length = 3;
    } else if ((byte & 0xF8U) == 0xF0U && i + 3U < utf8.size()) {
      codepoint = ((byte & 0x07U) << 18U) |
                  ((static_cast<u8>(utf8[i + 1U]) & 0x3FU) << 12U) |
                  ((static_cast<u8>(utf8[i + 2U]) & 0x3FU) << 6U) |
                  (static_cast<u8>(utf8[i + 3U]) & 0x3FU);
      length = 4;
    }

    decoded.push_back({codepoint, begin, begin + length});
    i += length;
  }
  return decoded;
}

[[nodiscard]] f32 ft_26_6_to_pixels(FT_Pos value) noexcept {
  return static_cast<f32>(value) / 64.0F;
}

[[nodiscard]] u32 next_power_of_two(u32 value) noexcept {
  u32 result = 1;
  while (result < value) {
    result <<= 1U;
  }
  return result;
}

} // namespace

struct FontSystem::Impl {
  struct GlyphKey {
    u32 face{};
    u32 glyph{};

    constexpr bool operator==(const GlyphKey&) const = default;
  };

  struct GlyphInfo {
    u32 page{};
    Vec2i uv_min{};
    Vec2i uv_max{};
    Vec2i size{};
    Vec2i bearing{};
  };

  struct FontFaceRecord {
    AssetID asset_id{};
    std::vector<u8> data{};
    FT_Face face{};
    hb_font_t* hb_font{};
    u32 pixel_size{};
    std::unordered_map<u32, GlyphInfo> glyphs{};
  };

  FT_Library library{};
  bool initialized{};
  std::vector<FontFaceRecord> faces{{}};
  Image atlas{};
  u32 atlas_padding{1};
  u32 atlas_cursor_x{1};
  u32 atlas_cursor_y{1};
  u32 atlas_row_height{};
  bool atlas_dirty_flag{};

  Impl() {
    initialized = FT_Init_FreeType(&library) == 0;
    atlas.width = 1024;
    atlas.height = 1024;
    atlas.channels = 4;
    atlas.pixels.assign(static_cast<usize>(atlas.width) * atlas.height * 4U, 0U);
  }

  ~Impl() {
    for (FontFaceRecord& face : faces) {
      if (face.hb_font != nullptr) {
        hb_font_destroy(face.hb_font);
      }
      if (face.face != nullptr) {
        FT_Done_Face(face.face);
      }
    }
    if (initialized) {
      FT_Done_FreeType(library);
    }
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
    if (record == nullptr || record->face == nullptr || record->face->size == nullptr) {
      return {};
    }
    const FT_Size_Metrics& size = record->face->size->metrics;
    return {
        ft_26_6_to_pixels(size.ascender),
        ft_26_6_to_pixels(size.descender),
        ft_26_6_to_pixels(size.height)};
  }

  [[nodiscard]] Expected<GlyphInfo, AssetError> ensure_glyph(FontFaceHandle handle, u32 glyph_index) {
    FontFaceRecord* record = face(handle);
    if (record == nullptr) {
      return Expected<GlyphInfo, AssetError>::failure({{}, {}, "cache glyph", "invalid font face handle"});
    }
    const auto found = record->glyphs.find(glyph_index);
    if (found != record->glyphs.end()) {
      return Expected<GlyphInfo, AssetError>::success(found->second);
    }

    const FT_Int32 load_flags = FT_LOAD_RENDER | FT_LOAD_COLOR;
    if (FT_Load_Glyph(record->face, glyph_index, load_flags) != 0) {
      return Expected<GlyphInfo, AssetError>::failure({
          record->asset_id.value,
          {},
          "rasterize glyph",
          "FreeType failed to load glyph"});
    }

    const FT_GlyphSlot glyph = record->face->glyph;
    const FT_Bitmap& bitmap = glyph->bitmap;
    const u32 width = bitmap.width;
    const u32 height = bitmap.rows;
    const u32 padded_width = width + atlas_padding * 2U;
    const u32 padded_height = height + atlas_padding * 2U;
    if (atlas_cursor_x + padded_width >= atlas.width) {
      atlas_cursor_x = atlas_padding;
      atlas_cursor_y += atlas_row_height + atlas_padding;
      atlas_row_height = 0;
    }
    if (atlas_cursor_y + padded_height >= atlas.height) {
      return Expected<GlyphInfo, AssetError>::failure({
          record->asset_id.value,
          {},
          "cache glyph",
          "font atlas page is full"});
    }

    const u32 dst_x = atlas_cursor_x + atlas_padding;
    const u32 dst_y = atlas_cursor_y + atlas_padding;
    for (u32 y = 0; y < height; ++y) {
      for (u32 x = 0; x < width; ++x) {
        const usize dst = (static_cast<usize>(dst_y + y) * atlas.width + (dst_x + x)) * 4U;
        if (bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
          const usize src = static_cast<usize>(y) * bitmap.pitch + x * 4U;
          atlas.pixels[dst + 0U] = bitmap.buffer[src + 2U];
          atlas.pixels[dst + 1U] = bitmap.buffer[src + 1U];
          atlas.pixels[dst + 2U] = bitmap.buffer[src + 0U];
          atlas.pixels[dst + 3U] = bitmap.buffer[src + 3U];
        } else {
          const usize src = static_cast<usize>(y) * bitmap.pitch + x;
          const u8 coverage = bitmap.buffer == nullptr ? 0U : bitmap.buffer[src];
          atlas.pixels[dst + 0U] = 255U;
          atlas.pixels[dst + 1U] = 255U;
          atlas.pixels[dst + 2U] = 255U;
          atlas.pixels[dst + 3U] = coverage;
        }
      }
    }

    GlyphInfo info;
    info.page = 0;
    info.uv_min = {static_cast<i32>(dst_x), static_cast<i32>(dst_y)};
    info.uv_max = {static_cast<i32>(dst_x + width), static_cast<i32>(dst_y + height)};
    info.size = {static_cast<i32>(width), static_cast<i32>(height)};
    info.bearing = {glyph->bitmap_left, glyph->bitmap_top};
    record->glyphs.emplace(glyph_index, info);
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
  return impl_ != nullptr && impl_->initialized;
}

FontFaceResult FontSystem::load_face(
    const std::filesystem::path& path,
    u32 pixel_size,
    AssetID asset_id) {
  if (!initialized()) {
    return FontFaceResult::failure({asset_id.value, path, "load font", "FreeType did not initialize"});
  }
  if (pixel_size == 0) {
    return FontFaceResult::failure({asset_id.value, path, "load font", "font pixel size must be positive"});
  }

  auto bytes = read_binary_file(path, asset_id);
  if (!bytes) {
    return FontFaceResult::failure(bytes.error());
  }

  Impl::FontFaceRecord record;
  record.asset_id = std::move(asset_id);
  record.data = std::move(bytes.value());
  record.pixel_size = pixel_size;
  if (FT_New_Memory_Face(
          impl_->library,
          record.data.data(),
          static_cast<FT_Long>(record.data.size()),
          0,
          &record.face) != 0) {
    return FontFaceResult::failure({record.asset_id.value, path, "load font", "FreeType could not load face"});
  }
  if (FT_Set_Pixel_Sizes(record.face, 0, pixel_size) != 0) {
    FT_Done_Face(record.face);
    return FontFaceResult::failure({record.asset_id.value, path, "load font", "FreeType could not set pixel size"});
  }

  record.hb_font = hb_ft_font_create_referenced(record.face);
  if (record.hb_font == nullptr) {
    FT_Done_Face(record.face);
    return FontFaceResult::failure({record.asset_id.value, path, "load font", "HarfBuzz could not create font"});
  }

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
  if (stack.faces.empty()) {
    return std::nullopt;
  }
  for (const FontFaceHandle handle : stack.faces) {
    const Impl::FontFaceRecord* record = impl_->face(handle);
    if (record != nullptr && FT_Get_Char_Index(record->face, static_cast<FT_ULong>(codepoint)) != 0) {
      return handle;
    }
  }
  return stack.faces.front();
}

FontSpansResult FontSystem::fallback_spans(std::string_view utf8, const FontStack& stack) const {
  if (stack.faces.empty()) {
    return FontSpansResult::failure({{}, {}, "resolve font fallback", "font stack is empty"});
  }

  std::vector<FontSpan> spans;
  for (const DecodedCodepoint& decoded : decode_utf8(utf8)) {
    auto selected = select_font_for_codepoint(stack, decoded.codepoint);
    if (!selected) {
      return FontSpansResult::failure({{}, {}, "resolve font fallback", "no usable fonts in stack"});
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
  if (stack.faces.empty()) {
    return TextMeasureResult::failure({{}, {}, "measure text", "font stack is empty"});
  }

  const FontMetrics base_metrics = metrics(stack.faces.front());
  f32 max_width = 0.0F;
  f32 line_width = 0.0F;
  u32 line_count = 1;

  usize line_begin = 0;
  while (line_begin <= utf8.size()) {
    const usize newline = utf8.find('\n', line_begin);
    const usize line_end = newline == std::string_view::npos ? utf8.size() : newline;
    std::string_view line = utf8.substr(line_begin, line_end - line_begin);
    auto spans = fallback_spans(line, stack);
    if (!spans) {
      return TextMeasureResult::failure(spans.error());
    }
    line_width = 0.0F;
    for (const FontSpan& span : spans.value()) {
      const Impl::FontFaceRecord* record = impl_->face(span.face);
      if (record == nullptr) {
        continue;
      }
      hb_buffer_t* buffer = hb_buffer_create();
      hb_buffer_add_utf8(
          buffer,
          line.data(),
          static_cast<int>(line.size()),
          static_cast<unsigned int>(span.byte_begin),
          static_cast<int>(span.byte_end - span.byte_begin));
      if (direction == TextDirection::Ltr) {
        hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
      } else if (direction == TextDirection::Rtl) {
        hb_buffer_set_direction(buffer, HB_DIRECTION_RTL);
      } else {
        hb_buffer_guess_segment_properties(buffer);
      }
      hb_shape(record->hb_font, buffer, nullptr, 0);
      unsigned int count = 0;
      const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
      for (unsigned int i = 0; i < count; ++i) {
        line_width += static_cast<f32>(positions[i].x_advance) / 64.0F;
      }
      hb_buffer_destroy(buffer);
    }
    max_width = std::max(max_width, line_width);
    if (newline == std::string_view::npos) {
      break;
    }
    ++line_count;
    line_begin = line_end + 1U;
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
  auto measure = measure_text(utf8, stack, direction);
  if (!measure) {
    return TextLayoutResult::failure(measure.error());
  }

  const FontMetrics base_metrics = metrics(stack.faces.front());
  TextLayout layout;
  layout.measure = measure.value();
  f32 pen_y = origin.y + base_metrics.ascender;
  usize line_begin = 0;
  while (line_begin <= utf8.size()) {
    const usize newline = utf8.find('\n', line_begin);
    const usize line_end = newline == std::string_view::npos ? utf8.size() : newline;
    std::string_view line = utf8.substr(line_begin, line_end - line_begin);
    auto spans = fallback_spans(line, stack);
    if (!spans) {
      return TextLayoutResult::failure(spans.error());
    }
    f32 pen_x = origin.x;
    for (const FontSpan& span : spans.value()) {
      Impl::FontFaceRecord* record = impl_->face(span.face);
      if (record == nullptr) {
        continue;
      }
      hb_buffer_t* buffer = hb_buffer_create();
      hb_buffer_add_utf8(
          buffer,
          line.data(),
          static_cast<int>(line.size()),
          static_cast<unsigned int>(span.byte_begin),
          static_cast<int>(span.byte_end - span.byte_begin));
      if (direction == TextDirection::Ltr) {
        hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
      } else if (direction == TextDirection::Rtl) {
        hb_buffer_set_direction(buffer, HB_DIRECTION_RTL);
      } else {
        hb_buffer_guess_segment_properties(buffer);
      }
      hb_shape(record->hb_font, buffer, nullptr, 0);

      unsigned int count = 0;
      const hb_glyph_info_t* infos = hb_buffer_get_glyph_infos(buffer, &count);
      const hb_glyph_position_t* positions = hb_buffer_get_glyph_positions(buffer, &count);
      for (unsigned int i = 0; i < count; ++i) {
        auto glyph = impl_->ensure_glyph(span.face, infos[i].codepoint);
        if (!glyph) {
          hb_buffer_destroy(buffer);
          return TextLayoutResult::failure(glyph.error());
        }
        const Impl::GlyphInfo info = glyph.value();
        const f32 x_offset = static_cast<f32>(positions[i].x_offset) / 64.0F;
        const f32 y_offset = static_cast<f32>(positions[i].y_offset) / 64.0F;
        const Vec2f glyph_min{
            pen_x + x_offset + static_cast<f32>(info.bearing.x),
            pen_y - y_offset - static_cast<f32>(info.bearing.y)};
        const Vec2f glyph_max{
            glyph_min.x + static_cast<f32>(info.size.x),
            glyph_min.y + static_cast<f32>(info.size.y)};
        if (info.size.x > 0 && info.size.y > 0) {
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
        pen_x += static_cast<f32>(positions[i].x_advance) / 64.0F;
        pen_y += static_cast<f32>(positions[i].y_advance) / 64.0F;
      }
      hb_buffer_destroy(buffer);
    }
    if (newline == std::string_view::npos) {
      break;
    }
    pen_y += base_metrics.line_height;
    line_begin = line_end + 1U;
  }

  return TextLayoutResult::success(std::move(layout));
}

FontMetrics FontSystem::metrics(FontFaceHandle face) const noexcept {
  return impl_->metrics(face);
}

usize FontSystem::cached_glyph_count(FontFaceHandle face) const noexcept {
  const Impl::FontFaceRecord* record = impl_->face(face);
  return record == nullptr ? 0U : record->glyphs.size();
}

const Image& FontSystem::atlas_image() const noexcept {
  return impl_->atlas;
}

bool FontSystem::atlas_dirty() const noexcept {
  return impl_->atlas_dirty_flag;
}

void FontSystem::clear_atlas_dirty() noexcept {
  impl_->atlas_dirty_flag = false;
}

} // namespace arv::asset
