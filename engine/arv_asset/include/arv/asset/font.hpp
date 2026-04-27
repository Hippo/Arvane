#pragma once

#include "arv/asset/resource.hpp"

#include <optional>
#include <memory>
#include <span>
#include <string_view>

namespace arv::asset {

struct FontFaceTag {};

using FontFaceID = ID<FontFaceTag, u32>;

struct FontFaceHandle {
  FontFaceID id{};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return id.value != 0;
  }

  constexpr bool operator==(const FontFaceHandle&) const = default;
};

enum class TextDirection : u8 {
  Auto,
  Ltr,
  Rtl,
};

struct FontStack {
  std::vector<FontFaceHandle> faces{};
};

struct FontMetrics {
  f32 ascender{};
  f32 descender{};
  f32 line_height{};
};

struct TextMeasure {
  f32 width{};
  f32 height{};
  f32 ascender{};
  f32 descender{};
  f32 line_height{};
};

struct FontSpan {
  FontFaceHandle face{};
  usize byte_begin{};
  usize byte_end{};
};

struct GlyphQuad {
  Vec2f min{};
  Vec2f max{};
  Vec2f uv_min{};
  Vec2f uv_max{};
  Vec4f color{1.0F, 1.0F, 1.0F, 1.0F};
  u32 page{};
};

struct TextLayout {
  std::vector<GlyphQuad> quads{};
  TextMeasure measure{};
};

using FontFaceResult = Expected<FontFaceHandle, AssetError>;
using FontSpansResult = Expected<std::vector<FontSpan>, AssetError>;
using TextMeasureResult = Expected<TextMeasure, AssetError>;
using TextLayoutResult = Expected<TextLayout, AssetError>;

class FontSystem {
public:
  FontSystem();
  FontSystem(const FontSystem&) = delete;
  FontSystem& operator=(const FontSystem&) = delete;
  FontSystem(FontSystem&&) noexcept;
  FontSystem& operator=(FontSystem&&) noexcept;
  ~FontSystem();

  [[nodiscard]] bool initialized() const noexcept;

  [[nodiscard]] FontFaceResult load_face(
      const std::filesystem::path& path,
      u32 pixel_size,
      AssetID asset_id);

  [[nodiscard]] FontStack make_stack(std::span<const FontFaceHandle> faces) const;

  [[nodiscard]] std::optional<FontFaceHandle> select_font_for_codepoint(
      const FontStack& stack,
      char32_t codepoint) const;

  [[nodiscard]] FontSpansResult fallback_spans(
      std::string_view utf8,
      const FontStack& stack) const;

  [[nodiscard]] TextMeasureResult measure_text(
      std::string_view utf8,
      const FontStack& stack,
      TextDirection direction = TextDirection::Auto) const;

  [[nodiscard]] TextLayoutResult layout_text(
      std::string_view utf8,
      const FontStack& stack,
      Vec2f origin,
      Vec4f color,
      TextDirection direction = TextDirection::Auto);

  [[nodiscard]] FontMetrics metrics(FontFaceHandle face) const noexcept;

  [[nodiscard]] usize cached_glyph_count(FontFaceHandle face) const noexcept;

  [[nodiscard]] const Image& atlas_image() const noexcept;

  [[nodiscard]] bool atlas_dirty() const noexcept;

  void clear_atlas_dirty() noexcept;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace arv::asset
