# Paged Texture Atlases

## Purpose

Arvane supports a paged texture atlas abstraction for dynamic and large texture
sets. A paged atlas is one `GL_TEXTURE_2D_ARRAY`; each texture-array layer is a
fixed-size atlas page, and each page owns independent CPU rectangle allocation
state.

The page index is the texture-array layer. Shaders sample paged atlases with:

```glsl
uniform sampler2DArray uAtlas;
vec4 color = texture(uAtlas, vec3(uv, page_index));
```

When the page is passed through vertex or instance data, it must be carried with
flat interpolation.

## Domains

Atlas domains separate texture lifetimes and update policies:

```cpp
enum class TextureAtlasDomain : u8 {
    Blocks,
    Entities,
    PlayerSkins,
    UiImages,
    Fonts,
    Particles,
};
```

Each domain owns its own atlas instance. Different domains may use different
page sizes, page counts, formats, mipmap policies, cleanup policies, and draw
groups. Sharing one shader and one texture array can batch across pages within a
domain, but different domains still require separate texture binds or draw
groups.

## Region Handles

Paged atlas allocations use stable handles:

```cpp
struct TextureRegionHandle {
    u32 id;
    u32 generation;
};
```

The handle resolves to:

```cpp
struct AtlasRegion {
    u16 page_index;
    u16 x;
    u16 y;
    u16 width;
    u16 height;
};
```

Generation checks reject stale handles after a region is freed. Cleanup never
moves live regions, so existing UVs and handles remain stable until the handle is
freed or evicted.

## GL Requirements

Paged atlases are backed by `GL_TEXTURE_2D_ARRAY`, not separate `GL_TEXTURE_2D`
objects. All pages in the array have identical width, height, internal format,
mipmap count, and sampler state.

Initialization queries `GL_MAX_TEXTURE_SIZE` and
`GL_MAX_ARRAY_TEXTURE_LAYERS`. Dynamic paged atlases clamp their configured page
sizes and page counts to the GPU limits before storage is allocated. Static
texture-array uploads reject over-limit arrays and fall back to the renderer's
white/default texture path rather than creating invalid GL storage. OpenGL
object creation, destruction, and texture uploads are render-thread-only.

## Player Skins

Player skins use the `PlayerSkins` domain. They are no longer inserted into the
authored entity atlas.

This keeps the responsibilities separate:

```text
entity atlas       = authored/static entity, item, outline, and overlay textures
player skin atlas  = dynamic user-generated textures with GPU residency cleanup
skin cache         = persistent validated PNG files on disk
```

Skin files are loaded from:

```text
arvane_home/skins/<skin_hash>.png
arvane_home/cache/skins/<skin_hash>.png
```

The disk cache is not GPU residency. Evicting a skin from the player skin atlas
frees only the texture-array region and never deletes either cached or local skin
files.

## Residency And Cleanup

A resident skin tracks its hash, texture region handle, dimensions, last-used
frame/time, and pin state. Visible player skins are marked used and pinned by
the renderer while they are drawn. If a requested skin is not resident, player
rendering uses the default skin until the PNG is decoded, uploaded on the render
thread, and assigned a resident handle.

Cleanup is conservative:

* periodic cleanup runs no more often than the configured interval
* periodic cleanup evicts only unpinned skins unused for the configured age
* pressure cleanup may run after allocation pressure and evicts least-recently
  used unpinned skins first
* cleanup does not compact or move live regions
* allocation failure falls back to the default skin path instead of crashing

The first implementation does not generate mipmaps for dynamic player skin
uploads. This avoids partial-mipmap update complexity; enabling mipmaps later
requires padding/extrusion and a clear regeneration policy.

## Static Atlas Domains

Blocks, authored entities, UI images, and fonts use texture-array uploads in the
renderer.

* block textures bind as the `Blocks` domain and are sampled by chunk shaders
* authored entity, item, outline, and break overlay textures bind as the
  `Entities` domain and are sampled by entity shaders
* UI item and terrain icon sources bind through UI texture-array slots
* font glyphs bind through the UI font texture-array slot
* player skins bind through the separate dynamic `PlayerSkins` domain

The CPU atlas builder can produce multiple fixed-size pages. Static upload data
copies those pages into one `GL_TEXTURE_2D_ARRAY`; page `N` in CPU metadata maps
directly to texture-array layer `N`.

Chunk vertices deliberately carry a separate flat `atlas_page` field. This grows
the chunk vertex from 12 bytes to 16 bytes and avoids stealing bits from the
packed lighting/material field. Future material-table work may replace this with
a material or texture id if chunk bandwidth becomes the dominant cost.

UI vertices carry `atlas_page` beside the texture slot. The slot selects the UI
domain binding, while the page selects the texture-array layer inside that
domain. UI batches can therefore include icons or glyphs from different pages as
long as they share the same shader and bound texture arrays.

Font metadata already records glyph page indices. The current font atlas
allocator may still be conservative about creating new CPU pages, but the
renderer-side contract is page-aware and texture-array-backed.

Particles are reserved as an atlas domain. They should follow the same
texture-array contract when particle rendering is introduced.
