# Font Rendering

## Purpose

This document defines Arvane’s font rendering system.

The font renderer is responsible for:

- loading fonts
- shaping text
- resolving font fallback
- caching glyphs
- maintaining glyph atlases
- producing UI-renderable text geometry

The system is designed to support user-facing UI, debug UI overlays where appropriate, localization, symbols, icons, and mixed-font text.

Arvane uses:

- **FreeType** for font loading, rasterization, glyph metrics, and glyph bitmap generation
- **HarfBuzz** for text shaping, glyph substitution, kerning, bidirectional-aware shaping, and script/language-sensitive layout

---

## Design Goals

The font renderer must be:

- versatile enough for general UI text
- capable of rendering Unicode text with fallback fonts
- compatible with Arvane’s retained UI system
- usable by both engine UI and plugin-provided UI
- deterministic enough for layout measurement
- efficient enough for frequently changing UI text
- isolated from game simulation logic

The font renderer must not assume ASCII-only text.

---

## Non-Goals

The initial font renderer does not need to implement:

- full rich-text document layout
- editable multiline text fields
- complex paragraph layout
- font downloading
- signed distance field generation
- vector text rendering
- GPU path rendering

These may be added later, but the base renderer should remain compatible with them.

---

## Core Concepts

### Font Face

A font face represents a loaded font file at a specific pixel size.

A font face owns:

- FreeType face data
- HarfBuzz font object
- font metrics
- glyph cache
- glyph atlas pages for that face and size

A font face is size-specific.

For example:

- `Inter-Regular.ttf` at 16px
- `Inter-Regular.ttf` at 24px`
- `NotoColorEmoji.ttf` at 24px`

are distinct font faces.

---

### Font Stack

Text rendering must use a **font stack** rather than a single font.

A font stack is an ordered list of font faces used for fallback.

Example:

```text
Primary UI font
Symbol font
CJK fallback font
Arabic fallback font
Emoji font
```

When a codepoint is not available in the primary font, the renderer searches the remaining fonts in order.

The first font that supports the codepoint is used.

The first font in the stack defines the default metrics for layout unless explicitly overridden.

---

### Font Fallback

Fallback resolution happens before shaping by splitting text into font spans.

A font span is a contiguous byte range of UTF-8 text that should be shaped with a single font face.

Example:

```text
"Hello ✔ 😂"
```

may become:

```text
[Primary font] "Hello "
[Symbol font]  "✔"
[Emoji font]   "😂"
```

Fallback must preserve UTF-8 byte ranges so that text can be shaped without copying where possible.

If no font supports a codepoint, the renderer should use the primary font’s missing-glyph representation if available.

---

## FreeType Usage

FreeType is responsible for:

* loading font files
* selecting pixel size
* checking whether a font supports a codepoint
* loading glyphs by glyph index
* rasterizing glyph bitmaps
* retrieving glyph bearings
* retrieving glyph dimensions
* retrieving ascender, descender, and line height metrics

Glyphs should be loaded by **glyph index**, not by Unicode codepoint after shaping.

This is important because HarfBuzz may map characters to different glyph indices through ligatures, substitutions, or script-specific shaping.

---

## HarfBuzz Usage

HarfBuzz is responsible for shaping text.

For each font span, the renderer must:

1. create a HarfBuzz buffer
2. add the UTF-8 span to the buffer
3. set or infer text direction
4. set language when provided
5. let HarfBuzz guess segment properties where appropriate
6. shape the buffer using the span’s font
7. read glyph indices, advances, and offsets

The shaping result should contain:

* glyph index
* x advance
* y advance
* x offset
* y offset

All HarfBuzz 26.6 fixed-point positions should be converted into pixel-space floating-point values.

---

## Text Direction

The font renderer must support:

* left-to-right text
* right-to-left text
* language-aware shaping

The UI system may expose explicit direction controls, but the font renderer should also support automatic segment-property guessing through HarfBuzz.

For early implementation, direction may be provided by the caller as:

```text
LTR
RTL
Auto
```

`Auto` should allow HarfBuzz to infer the segment properties from the text.

---

## Glyph Cache

Each font face maintains a glyph cache keyed by glyph index.

Cached glyph data should include:

* atlas page index
* UV coordinates
* bitmap width
* bitmap height
* bearing X
* bearing Y
* glyph format information if needed

Glyphs are loaded lazily.

A glyph should only be rasterized and inserted into the atlas when it is first needed for rendering.

---

## Glyph Atlas

Each font face owns one or more glyph atlas pages.

Atlas pages are GPU textures containing rasterized glyph bitmaps.

The atlas system must support:

* multiple pages
* lazy page allocation
* padding around glyphs
* alpha glyphs
* color glyphs where supported by FreeType

A default page size of `1024x1024` is reasonable, but this should be configurable.

Glyphs should be padded by at least one pixel to avoid sampling bleed.

The general renderer atlas model is described in
[Paged Texture Atlases](114_paged_texture_atlases.md). Font metadata already
tracks glyph page indices, and the UI renderer samples the font binding as a
`GL_TEXTURE_2D_ARRAY`. Glyph page indices must be passed flat to the UI/text
shader and sampled as the array layer. The font allocator may still choose to
stay on one CPU page until multi-page glyph allocation is expanded, but the
rendering contract is page-aware.

---

## Glyph Bitmap Formats

The renderer must support at minimum:

### Alpha Glyphs

Standard monochrome/antialiased glyphs are uploaded as white RGB with glyph coverage stored in alpha.

The text color is applied by the UI shader.

### Color Glyphs

Color emoji and color fonts may produce BGRA bitmap data.

When FreeType provides color glyph bitmaps, the renderer should upload the glyph color directly.

The shader must be able to distinguish between normal alpha glyphs and color glyphs if color multiplication would produce incorrect results.

Initial implementation may multiply color glyphs by white and treat user color as a tint only for non-color glyphs.

---

## Text Geometry

Text is rendered as quads.

Each visible glyph produces one quad using:

* screen-space position
* size
* atlas UV coordinates
* texture page reference
* color
* optional text-specific flags

Glyph quad placement uses:

```text
glyph_x = pen_x + glyph_x_offset + glyph_bearing_x
glyph_y = baseline_y - glyph_y_offset - glyph_bearing_y
```

After emitting the glyph, the pen advances by the shaped glyph advance.

Newlines reset `pen_x` to the original line start and advance `pen_y` by the font stack line height.

---

## Layout and Measurement

The font renderer must provide measurement APIs separate from drawing.

Required measurement operations:

* measure a single line
* measure multiline text
* compute line height
* compute ascender
* compute descender
* compute baseline offset
* compute glyph run bounds

UI layout must be able to measure text before rendering it.

Text measurement must use the same shaping and fallback rules as rendering.

---

## DPI and UI Scale

Font rendering must respect UI scale.

The UI system may use logical units, but glyph rasterization is pixel-size dependent.

The renderer should support either:

1. loading font faces at scaled pixel sizes, or
2. maintaining font size variants per scale bucket

The preferred approach is to recreate or lazily create font faces for the effective pixel size used by the UI.

Text should remain sharp on high-DPI displays.

---

## Integration With UI Renderer

The font renderer does not draw directly to the screen.

Instead, it produces text draw commands or appends text quads into the UI renderer’s batching system.

The UI renderer is responsible for:

* batching glyph quads
* binding atlas textures
* flushing when texture slots are exhausted
* applying clipping/scissor rectangles
* applying final transforms
* submitting draw calls

The font renderer is responsible for:

* shaping
* fallback
* glyph caching
* atlas updates
* text metrics
* glyph quad generation

---

## Texture Binding Model

Because each font face can have multiple atlas pages, text rendering must
support multiple texture-array layers.

The UI renderer may batch glyphs with other UI quads as long as texture binding limits are respected.

The page index is vertex data, not a separate texture binding. A batch only
needs to split when the font atlas domain binding, shader, or sampler state
changes.

---

## Plugin UI Support

Plugin-authored UI should be able to reference font styles abstractly.

Plugins should not directly load arbitrary fonts into the renderer without going through engine-managed APIs.

Allowed plugin-facing concepts:

* font family name
* font size
* font weight
* font style
* text color
* alignment
* wrapping behavior
* localization key or raw text

The engine resolves these into internal font stacks and font faces.

This prevents plugin UI from bypassing atlas ownership, renderer state, or asset policy.

---

## Font Assets

Fonts are engine assets.

Font definitions should be data-driven.

A font family definition may include:

```yaml
id: arvane:ui/default
faces:
  regular: fonts/Inter-Regular.ttf
  bold: fonts/Inter-Bold.ttf
  italic: fonts/Inter-Italic.ttf
fallbacks:
  - fonts/NotoSansSymbols-Regular.ttf
  - fonts/NotoSansArabic-Regular.ttf
  - fonts/NotoColorEmoji.ttf
```

The exact schema may evolve, but font stacks should be declared through assets rather than hard-coded in rendering code.

---

## Error Handling

Font loading failures must be reported clearly.

The renderer should handle:

* missing font files
* invalid font files
* unsupported glyphs
* atlas allocation failure
* glyph rasterization failure
* HarfBuzz shaping failure

A missing optional fallback font should not crash the engine.

A missing required primary UI font should be treated as an asset initialization error.

---

## Threading Model

Font loading and glyph rasterization may eventually happen off-thread, but initial implementation may keep font rendering on the render/UI thread.

The following rules must hold:

* GPU texture creation happens on the render thread
* GPU atlas updates happen on the render thread
* UI layout must not mutate font atlas state unless explicitly allowed
* background font preparation must hand completed raster data back to the render thread for upload

The first implementation should prefer correctness over asynchronous complexity.

---

## Caching and Lifetime

Font resources should be owned by the asset/resource system.

Font faces should remain alive while any UI draw commands reference their atlas pages.

Glyph atlas pages should be destroyed when their owning font face is destroyed.

The renderer must not retain raw references to unloaded font assets.

---

## Required Public Capabilities

The font system should expose engine-facing capabilities equivalent to:

```text
load font family
create font stack
measure text
shape text
draw text into UI batch
query line height
query ascender/descender
query whether a font supports a codepoint
```

The API should avoid exposing FreeType or HarfBuzz types outside the font module.

---

## Initial Implementation Checklist

The first implementation should include:

* FreeType initialization and shutdown
* HarfBuzz integration
* font face loading
* font stack fallback
* UTF-8 traversal
* shaping per font span
* lazy glyph rasterization
* glyph atlas page allocation
* glyph UV generation
* text quad emission
* newline handling
* alpha glyph rendering
* basic color emoji support if available through FreeType
* text measurement using the same path as rendering

---

## Invariants

The following invariants must hold:

* Text shaping uses HarfBuzz.
* Glyph rasterization uses FreeType.
* Rendered glyphs are addressed by glyph index, not Unicode codepoint.
* Font fallback is ordered and deterministic.
* The first font in the stack defines default line metrics.
* UI layout and UI rendering use the same text measurement logic.
* Glyph atlas updates are owned by the font renderer.
* GPU uploads occur only on a valid render context thread.
* The font renderer does not depend on gameplay systems.
