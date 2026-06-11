# Assets and Resources

## Purpose

This document defines how Arvane loads, owns, caches, builds, and reloads assets.

The asset system must provide a clear boundary between:

- authored files on disk
- decoded CPU-side asset data
- runtime engine resources
- GPU resources
- game/plugin-defined content

Assets must not be loaded ad hoc from random systems. Systems should request assets through the asset/resource layer and receive stable handles or resolved runtime resources.

---

## Pack Asset Layout

Reloadable authored assets live in enabled packs under `arvane_home/packs/`.
The built-in content pack is `arvane_base`, and it provides the `arvane`
namespace.

Pack asset folders must remain **cleanly structured, predictable, and stable**,
as rendering, UI, and gameplay presentation systems resolve assets through
namespaced resource IDs.

### Directory Structure

```
arvane_home/
  packs/
    enabled.yaml
    arvane_base/
      pack.yaml
      assets/
        arvane/
          textures/
            block/
            entity/
            ui/
          models/
            block/
            entity/
          animations/
          shaders/
          fonts/
          materials/
          ui/
          audio/
      data/
        arvane/
  config/
```

`arvane_home/` is the runtime home directory for the engine/game. In development it lives at the project root by default and also contains `config/`, `saves/`, `skins/`, and optional `logs/` / `cache/` directories.

Systems should resolve asset paths through the central filesystem/path utility instead of hardcoding root-relative paths.

### Asset Subtree

```
assets/<namespace>/
  textures/
    block/
    entity/
    ui/
  models/
    block/
    entity/
  animations/
  shaders/
  fonts/
  materials/
  ui/
  audio/
```

---

## Directory Breakdown

### `textures/`

Contains all image-based assets used for rendering.

```
  textures/
    block/    # terrain/block textures used in atlas generation
    entity/   # entity textures (players, mobs, etc.)
    particle/ # particle textures and animation frames
    default_skin/ # fallback player skin texture
    ui/       # UI textures (icons, panels, etc.)
  font/     # bitmap font atlases (if generated or cached)
```

**Notes:**

* Block textures are typically packed into a **runtime atlas**
* Texture resolution does not need to match logical UV size
* Mip padding rules must be respected (see material/atlas section)
* The default player skin assets are
  `arvane:texture/default_skin/default` and
  `arvane:texture/default_skin/default_slim`
* Particle textures are packed into a dedicated runtime texture-array atlas.
  Animated particle frames such as `arvane:texture/particle/explosion_0`
  resolve to atlas entries during asset loading, not during rendering.

User-provided skins are untrusted binary assets. Local profile-owned skins live
in `arvane_home/skins/`; server-downloaded skins live in
`arvane_home/cache/skins/`. They are decoded and validated through the asset
PNG loader, but they are not authored pack assets.
Servers store accepted uploads under the active save root at
`player_assets/skins/` by server-computed content hash.

---

### `models/`

Defines geometry for blocks and entities. Block and entity models use the same
authored cuboid schema.

```
models/
  block/
  entity/
```

**Notes:**

* Models are **data-driven** (e.g. JSON)
* Entity models are geometry-only assets
* Support multiple textures per model
* Each model has top-level `bones`; every box references one declared bone
* Bone pivots/rotations and cube origins/rotations are separate model fields
* Used by meshing and rendering systems

---

### `animations/`

Defines separate entity presentation animation assets.

```
animations/
```

**Notes:**

* Animation assets are JSON files
* Asset IDs use `arvane:animation/name`
* `arvane:animation/pig` resolves to `assets/arvane/animations/pig.json` in the owning pack
* Keyframes reference authored model `boxes[].bone` names by string
* Animation assets are client presentation data, not simulation behavior

---

### `shaders/`

Contains all GPU shader sources.

```
shaders/
  core/        # shared/common shader code (includes, utilities)
  world/       # chunk/block rendering
  entity/      # entity rendering
  ui/          # UI rendering
  particle/    # instanced billboard particles
  post/        # post-processing (fog, composite, etc.)
```

**Notes:**

* Organized by render pipeline stage
* Loaded and compiled at runtime
* May support hot-reload in development builds

---

### `fonts/`

Font files used by the UI and text rendering system.

```
fonts/
```

**Notes:**

* Typically `.ttf` / `.otf`
* Used with FreeType + HarfBuzz
* Combined into runtime glyph atlases

---

### `materials/`

Defines material configurations used by the renderer.

```
materials/
```

**Notes:**

* Maps textures + shader parameters
* May define blending modes, transparency, etc.
* Used to bind rendering state consistently

---

### `ui/`

UI layout and styling definitions.

```
ui/
```

**Notes:**

* Retained UI definitions (data-driven)
* May include layout trees, styles, and bindings
* Can be extended by plugins

---

### `arvane_home/config/`

Game and engine configuration files.

```
arvane_home/config/
```

**Notes:**

* YAML or JSON format
* Includes:

  * block definitions
  * world generation settings
  * gameplay tuning values

---

### `audio/`

Sound and music assets.

```
audio/
  sfx/
  music/
```

**Notes:**

* Used by audio subsystem
* Supports streaming for large assets (music)

---

## General Rules

* All assets must be **data-driven and reloadable**
* File formats must be **explicitly defined and consistent**
* Systems must not hardcode asset paths outside this structure
* New asset types should be added in a **clearly scoped subdirectory**
* Plugins may introduce additional subdirectories, but must not break core structure

---

## Asset Types

Arvane supports several broad asset categories.

### Core Asset Types

Common engine asset types include:

- textures
- texture atlases
- shaders
- materials
- block definitions
- item definitions
- entity definitions
- models
- animations
- fonts
- sounds
- localization files
- configuration files

Not every asset type needs to be implemented immediately, but the asset system should be structured so new asset types can be added without changing unrelated systems.

---

## Authored Assets vs Runtime Resources

The engine distinguishes between authored assets and runtime resources.

### Authored Assets

Authored assets are files provided by the engine, game layer, plugins, or resource packs.

Examples:

- `stone.png`
- `block_definitions.json`
- `chunk_opaque.vert`
- `chunk_opaque.frag`
- `ui_font.ttf`

Authored assets are identified by stable logical asset names, not by raw filesystem paths in gameplay code.

Example:

```cpp
arvane:block/stone
arvane:shader/chunk_opaque
arvane:texture/block/grass_top
```

### Runtime Resources

Runtime resources are processed objects used by the engine.

Examples:

* decoded image data
* packed texture atlases
* compiled shader programs
* uploaded GPU textures
* parsed block definitions
* material instances

Runtime resources may be recreated when authored assets change.

Gameplay and engine systems should not assume runtime resource pointers remain valid forever unless explicitly documented.

---

## Asset Identity

Assets are identified by namespaced asset identifiers.

Example format:

```txt
namespace:path/to/asset
```

Examples:

```txt
arvane:block/stone
arvane:texture/block/dirt
arvane:model/block/stairs
arvane:model/entity/pig
arvane:animation/pig
arvane:shader/world/chunk_opaque
mod_example:block/marble
```

Rules:

* asset identifiers are stable authored names
* asset identifiers are not numeric runtime IDs
* asset identifiers must not include file extensions
* the same logical asset must resolve consistently across reloads
* namespace ownership determines conflict behavior

Runtime systems may assign internal handles or IDs for fast lookup, but authored content should refer to stable asset identifiers.

---

## Resource Ownership

The asset/resource system owns loaded resources.

Engine systems may hold:

* asset handles
* weak references
* cached resolved pointers valid for a frame or known lifetime
* GPU handles exposed through renderer-managed abstractions

Engine systems must not directly own shared global copies of loaded assets.

### Ownership Responsibilities

The asset system is responsible for:

* locating asset files
* decoding asset files
* validating asset data
* caching loaded assets
* tracking dependencies
* detecting reload changes
* publishing updated resources

The renderer is responsible for:

* GPU texture creation
* GPU buffer creation
* shader compilation/linking
* GPU resource destruction
* renderer-side resource lifetime

The game layer is responsible for:

* declaring gameplay asset references
* defining content semantics
* responding to failed asset loads when appropriate

---

## Asset Loading Pipeline

Asset loading should follow a staged pipeline.

```txt
Asset ID
  -> resolve path
  -> read file
  -> decode/parse
  -> validate
  -> build runtime resource
  -> upload/compile if needed
  -> publish handle
```

### Stage 1: Resolve

The asset system maps an asset identifier to one or more possible source files.

Example:

```txt
arvane:texture/block/stone
  -> arvane_home/packs/arvane_base/assets/arvane/textures/block/stone.png
```

Resolution uses the enabled pack stack. Later packs have higher priority for
eligible asset overrides, so a user texture pack can replace a base texture
without changing the `arvane:texture/block/stone` ID.

Resolution order must remain explicit and deterministic.

### Stage 2: Read

File I/O is performed by the asset system, not by random engine systems.

Reads may happen asynchronously where safe.

### Stage 3: Decode or Parse

Raw files are converted into CPU-side representations.

Examples:

* PNG/JPEG/WebP → image pixels
* JSON/YAML/TOML → parsed data structures
* GLSL source → shader source strings
* TTF/OTF → font data

Parsing should produce useful errors that include:

* asset identifier
* source file path
* line/column if available
* reason for failure

### Stage 4: Validate

Assets must be validated before being published.

Validation examples:

* texture dimensions are supported
* block textures exist
* shader stages are present
* required material fields exist
* atlas texture sizes match expected constraints
* invalid references are reported clearly

### Stage 5: Build Runtime Resource

Decoded data is converted into runtime resources.

Examples:

* texture atlas pages
* block texture index tables
* material lookup records
* shader program descriptions
* font glyph atlases

### Stage 6: Upload or Compile

Renderer-owned resources are created on the render thread or through renderer-approved upload queues.

Examples:

* OpenGL textures
* shader programs
* GPU buffers
* sampler objects

Non-render systems must not call OpenGL directly.

### Stage 7: Publish

The completed resource is published to the cache.

Existing handles should either:

* resolve to the new resource version, or
* remain valid until the old resource is safely retired

The behavior must be explicit per resource type.

---

## Resource Handles

Runtime systems should use handles instead of raw ownership.

Example:

```cpp
TextureHandle
ShaderHandle
MaterialHandle
AtlasHandle
BlockTextureHandle
```

Handles should be lightweight, copyable values.

A handle may internally contain:

* typed resource ID
* generation/version
* asset identifier reference
* cache index

Handles should prevent accidental mixing of unrelated resource types.

Invalid handles must be representable.

Example:

```cpp
TextureHandle::invalid()
ShaderHandle::invalid()
```

---

## Caching Rules

The asset system should cache loaded resources.

Caching rules:

* repeated requests for the same asset should not duplicate runtime resources
* resource caches should be type-specific
* cache entries should track load status
* failed loads may be cached briefly to avoid repeated spam
* cache invalidation must be versioned
* hot reload must update cache entries safely

A cache entry may be in one of several states:

```txt
Unloaded
Loading
Loaded
Failed
Reloading
```

Systems requesting a resource must handle missing or failed resources gracefully.

---

## Threading Model

Asset file loading and CPU-side decoding may run on worker threads.

GPU resource creation must happen through the renderer.

Rules:

* worker threads may read files
* worker threads may decode images
* worker threads may parse definitions
* worker threads may build CPU-side atlas data
* worker threads must not call OpenGL
* renderer upload happens through a render-safe command queue
* published resources must be synchronized safely

The asset system should prefer immutable snapshots for data passed across threads.

---

## Texture Assets

Texture assets are authored image files.

Supported formats should be explicitly listed by the implementation.

Initial expected format:

```txt
PNG
```

Additional formats may be added later only if the asset pipeline documents them.

Texture loading must define:

* color format
* alpha handling
* channel order
* sRGB expectations
* mipmap generation behavior
* filtering behavior
* wrap behavior

### Texture Color Space

Color textures should be treated as sRGB where appropriate.

Examples:

* block albedo textures: sRGB
* UI color textures: sRGB
* normal maps: linear
* data textures: linear

The asset metadata or texture usage must determine color-space behavior.

---

## Block Texture Layout

Block textures are authored as individual texture files.

Example:

```txt
arvane_home/packs/arvane_base/assets/arvane/textures/block/stone.png
arvane_home/packs/arvane_base/assets/arvane/textures/block/dirt.png
arvane_home/packs/arvane_base/assets/arvane/textures/block/grass_top.png
arvane_home/packs/arvane_base/assets/arvane/textures/block/grass_side.png
arvane_home/packs/arvane_base/assets/arvane/textures/block/grass_bottom.png
```

Block definitions reference texture asset identifiers.

Example:

```json
{
  "id": "arvane:grass",
  "textures": {
    "top": "arvane:texture/block/grass_top",
    "side": "arvane:texture/block/grass_side",
    "bottom": "arvane:texture/block/dirt",
    "particle": "arvane:texture/block/dirt"
  }
}
```

Texture references should be semantic.

Common block texture slots:

```txt
all
top
bottom
side
north
south
east
west
up
down
particle
```

The block system resolves face slots into per-face texture indices used by
meshing. The `particle` slot is not a rendered face; it selects the block dust
texture used by sprint, mining, and break particles. If `textures` is authored as
a single string, that same texture is used for every face and for the block
particle texture.

---

## Texture Atlas

Voxel block textures are packed into one or more texture atlases.

The texture atlas exists so chunk meshes can reference compact texture indices instead of binding many textures.

### Atlas Inputs

Atlas building takes:

* block texture asset references
* decoded image data
* texture usage metadata
* padding rules
* mipmap rules

### Atlas Outputs

Atlas building produces:

* atlas image pages
* atlas metadata
* texture index table
* UV rectangles
* per-texture padding information
* GPU texture handles after upload

The mesher should not know file paths.

The mesher should only need compact texture indices or atlas references resolved from block states.

---

## Atlas Page Rules

The atlas builder may create multiple pages if needed.

Rules:

* each page has a fixed maximum size
* texture entries record page index
* texture entries record UV region
* texture entries record padded and unpadded regions
* texture entries must be stable for a given atlas build
* atlas rebuilds may change indices unless explicitly versioned

Initial implementation may use one atlas page if sufficient.

The design should not assume there will only ever be one atlas texture.

---

## Texture Size Rules

Block textures should follow consistent dimensions.

Recommended initial rule:

```txt
All block textures in the same atlas should have the same base resolution.
```

Example:

```txt
16x16
32x32
64x64
```

The engine may support mixed-size textures later, but the first implementation should avoid this unless required.

Validation should reject:

* missing textures
* non-power-of-two block textures if unsupported
* textures larger than the configured maximum
* textures with unexpected dimensions
* invalid alpha formats for opaque-only blocks

---

## Mip Padding Strategy

Atlas padding is mandatory.

Without padding, mipmaps will bleed neighboring atlas; entries into each other at distance.

Each atlas entry must include duplicated edge padding around the original texture.

### Padding Rules

The atlas builder must:

* copy the original texture into the atlas
* duplicate edge pixels outward
* duplicate corners correctly
* reserve enough padding for mipmap generation
* generate mipmaps after padding is applied

Padding must be applied before mipmap generation.

### Minimum Padding

The minimum padding should be configurable.

Recommended initial value:

```txt
4 pixels
```

For higher-resolution textures or deeper mip chains, larger padding may be needed.

The implementation should avoid relying on a single-pixel padding strategy unless mipmap behavior has been verified.

### UV Rules

Mesh UVs must sample only the unpadded logical texture region.

The padded region exists only to protect filtering and mipmaps.

The mesher and shader must not intentionally sample into padding.

---

## Texture Filtering Expectations

Voxel block textures should preserve a crisp pixel-art style by default.

Initial filtering expectations:

```txt
magnification: nearest
minification: nearest_mipmap_nearest or nearest_mipmap_linear
wrap: clamp-to-edge for atlas textures
mipmaps: enabled for world rendering
```

The exact minification filter should be configurable, but the default should avoid blurry terrain.

Atlas textures must not use repeat wrapping because neighboring atlas entries would bleed.

For block textures requiring animation or special sampling, rules must be defined in the material system or texture metadata.

---

## Atlas Metadata

The atlas builder must produce metadata that can be consumed by the block, meshing, and rendering systems.

Example metadata:

```cpp
struct AtlasEntry {
  TextureAtlasPageID page;
  u32 texture_index;

  Vec2 uv_min;
  Vec2 uv_max;

  Vec2 unpadded_pixel_min;
  Vec2 unpadded_pixel_max;

  Vec2 padded_pixel_min;
  Vec2 padded_pixel_max;
};
```

The exact structure may differ, but it must expose:

* page
* stable lookup key
* runtime texture index
* UV rectangle
* dimensions
* padding information

Block states should resolve to atlas texture indices during registry finalization or resource baking.

---

## Block Texture Resolution

The block registry and asset system work together.

A block definition names textures by asset ID.

During registry finalization:

```txt
block texture asset IDs
  -> resolved texture assets
  -> atlas entries
  -> per-face texture indices
  -> block state render data
```

The mesher should receive finalized render data.

It should not resolve asset identifiers while generating chunk meshes.

---

## Animated Block Textures

Animated block textures may be supported later.

If supported, animation metadata should be explicit.

Example:

```json
{
  "texture": "arvane:texture/block/water",
  "animation": {
    "frame_time_ticks": 4,
    "frames": 16
  }
}
```

Rules:

* animation frames must share dimensions
* frame layout must be documented
* animation timing should use simulation ticks or render time consistently
* animated atlas regions must avoid bleeding between frames
* animated textures may require array textures or special atlas layout

Until implemented, animated textures should be rejected or treated as static first-frame textures with a warning.

---

## Shader Source Organization

Shader source files should live in a predictable folder structure.

Example:

```txt
arvane_home/packs/arvane_base/assets/arvane/shaders/
  world/
    chunk_opaque.vert
    chunk_opaque.frag
    chunk_transparent.vert
    chunk_transparent.frag
  entity/
    entity.vert
    entity.frag
  ui/
    ui.vert
    ui.frag
  common/
    camera.glsl
    lighting.glsl
    fog.glsl
    atlas.glsl
```

Shaders should be identified by asset ID.

Example:

```txt
arvane:shader/world/chunk_opaque
```

A shader asset may reference multiple stage files.

Example:

```json
{
  "id": "arvane:shader/world/chunk_opaque",
  "stages": {
    "vertex": "arvane:shader_source/world/chunk_opaque.vert",
    "fragment": "arvane:shader_source/world/chunk_opaque.frag"
  }
}
```

---

## Shader Includes

Shader code may use shared include files.

Rules:

* includes must be resolved by the shader asset system
* include paths must be asset-relative or namespace-relative
* include dependency tracking must support hot reload
* include cycles are invalid
* shader compile errors must include expanded source locations when possible

Example:

```glsl
#include "arvane:shader/common/lighting.glsl"
```

The exact include syntax may be implementation-defined, but it must be consistent.

---

## Shader Compilation

Shader compilation is renderer-owned.

The asset system may load and preprocess source, but OpenGL shader creation must occur in the renderer layer.

Shader compilation errors must include:

* shader asset ID
* stage
* source file
* line number if possible
* driver error output

Failed shader reloads should not destroy the last known-good shader unless explicitly requested.

---

## Materials

Materials connect shader programs, textures, render state, and parameters.

Material assets may define:

* shader reference
* texture references
* transparency mode
* culling mode
* depth behavior
* uniform/default parameter values

Voxel block rendering may use compact material IDs or render-layer IDs rather than one full material per block.

Material complexity should not leak into the chunk mesher unless needed for face grouping.

---

## Resource Packs and Overrides

The asset system should eventually support layered asset sources.

Example priority:

```txt
development override folder
enabled resource packs
enabled plugins
game assets
engine fallback assets
```

Higher-priority sources may override lower-priority sources.

Rules:

* override order must be deterministic
* asset conflicts must be diagnosable
* missing override assets should fall back only where valid
* resource pack reload should rebuild dependent resources

Initial implementation may only support engine/game assets, but should not hard-code assumptions that prevent layering later.

---

## Hot Reload

Hot reload is optional but valuable during development.

Initial hot reload behavior should target:

* shaders
* textures
* block definitions
* materials

Hot reload may be disabled in release builds.

### Hot Reload Rules

When a source file changes:

```txt
detect change
  -> identify asset
  -> reload asset
  -> validate
  -> rebuild dependent resources
  -> publish new version
```

Dependency tracking is required for correct hot reload.

Examples:

* changing `stone.png` rebuilds the block atlas
* changing `lighting.glsl` recompiles shaders that include it
* changing a block definition may rebuild registry-derived render data
* changing atlas inputs may invalidate chunk meshes

### Failed Reloads

Failed reloads must not crash the engine during development.

Rules:

* keep last known-good resource where possible
* report the error clearly
* mark the asset as failed/reload pending
* allow a later file save to retry

For critical registry assets, failed reload may require world reload or restart.

---

## Atlas Hot Reload

Texture atlas hot reload is more complex than individual texture reload.

Changing a block texture may require:

* decoding the changed texture
* rebuilding atlas pages
* uploading new atlas textures
* updating atlas metadata
* updating block render data
* invalidating affected chunk meshes

The engine may choose to invalidate all chunk meshes on atlas rebuild initially.

A later optimization may only invalidate chunks containing affected block states.

Initial rule:

```txt
Any block atlas rebuild marks all loaded chunk meshes dirty.
```

This is simple, correct, and acceptable for development.

---

## Resource Versioning

Resources that can reload should be versioned.

A handle should be able to detect whether its resolved resource changed.

Example:

```txt
TextureAtlas version 12
Shader version 7
Material version 3
```

Versioning helps systems decide whether cached derived data is stale.

Examples:

* chunk mesh references atlas version
* material cache references shader version
* UI font layout references font atlas version

If a resource version changes, dependent cached data must be validated or rebuilt.

---

## Missing Asset Behavior

Missing assets should produce clear fallback behavior.

Recommended fallbacks:

* missing texture: magenta/black checker texture
* missing shader: keep last known-good shader or use error shader
* missing block model/texture: use error block render data
* missing sound: silent no-op with warning
* missing font: fallback engine font if available

Missing assets must log an error.

Fallbacks are for development resilience, not a substitute for validation.

---

## Resource Lifetime

Resources should remain alive while referenced.

Rules:

* handles should not dangle
* renderer resources must be destroyed on the render thread or renderer-safe queue
* old resources replaced by hot reload should be retired safely
* resources used by in-flight frames must not be destroyed prematurely

The renderer may use delayed destruction for GPU resources.

Example:

```txt
publish new texture
old texture placed in deferred destroy queue
destroy after N frames
```

---

## Startup Asset Loading

Startup loading should load the minimum required assets to enter the game.

Required startup assets likely include:

* core shaders
* fallback texture
* block definitions
* block textures
* block atlas
* default font
* core materials

Large optional assets should be loaded lazily or asynchronously.

Startup should fail clearly if required core assets are invalid.

---

## Asset Manifests

The engine may use manifests to define asset groups.

Example:

```json
{
  "textures": [
    "arvane:texture/block/stone",
    "arvane:texture/block/dirt"
  ],
  "shaders": [
    "arvane:shader/world/chunk_opaque"
  ],
  "blocks": [
    "arvane:block/stone",
    "arvane:block/dirt"
  ]
}
```

Manifests can help with:

* startup loading
* resource pack validation
* dependency tracking
* tooling
* packaging

Manifests are optional for early implementation but useful for scaling.

---

## Asset Metadata

Some assets may require metadata.

Examples:

```json
{
  "color_space": "srgb",
  "filter": "nearest",
  "mipmaps": true,
  "wrap": "clamp_to_edge"
}
```

Metadata may be embedded in definition files or stored next to the asset.

Rules:

* defaults must be documented
* metadata should override defaults explicitly
* invalid metadata must fail validation
* metadata should not be silently ignored

---

## Error Handling

Asset errors should be structured and actionable.

An asset error should include:

* asset identifier
* file path
* asset type
* operation being performed
* detailed failure reason

Example:

```txt
Failed to build block atlas:
  asset: arvane:texture/block/grass_side
  path: arvane_home/packs/arvane_base/assets/arvane/textures/block/grass_side.png
  reason: texture size 17x16 does not match required block texture size 16x16
```

Avoid generic messages such as:

```txt
Failed to load texture
```

---

## Logging

Asset logging should distinguish:

* info
* warning
* error
* debug/trace

Examples:

* info: loaded core block atlas with 128 textures
* warning: texture uses fallback metadata
* error: missing texture referenced by block
* debug: shader include dependency discovered

Repeated asset errors should be rate-limited where appropriate.

---

## Tooling Expectations

The asset system should be tool-friendly.

Useful tooling outputs include:

* atlas preview image
* atlas metadata dump
* asset dependency graph
* missing asset report
* duplicate asset report
* shader include graph
* resource pack validation report

These tools do not need to exist immediately, but the asset pipeline should make them possible.

---

## Integration With Other Systems

### Blocks and Registry

Block definitions reference texture assets by stable asset ID.

The block registry consumes finalized render data after atlas build.

### Meshing

The mesher receives compact texture indices or render data from block states.

The mesher does not load assets.

The mesher does not inspect image files.

### Rendering

The renderer owns GPU resources.

The renderer consumes finalized shader, material, texture, and atlas resources.

### Lighting

Lighting does not depend on texture assets.

Lighting may influence shader/material behavior but should not be coupled to asset loading.

### UI

UI uses asset handles for fonts, textures, shaders, and materials.

UI should not directly load files.

### Plugins

Plugins may register asset namespaces and asset sources.

Plugins should not bypass the asset system for engine-visible resources.

---

## Initial Implementation Scope

The first implementation should support:

* namespaced asset IDs
* PNG texture loading
* shader source loading
* shader compilation through renderer
* block texture loading
* block/entity model loading
* entity animation asset loading
* block atlas build
* mip-safe atlas padding
* fallback missing texture
* resource handles
* basic caching
* clear validation errors

Hot reload may be implemented after the basic pipeline is stable.

---

## Non-Goals

The initial asset system does not need to support:

* full resource pack layering
* asset streaming from remote sources
* compressed GPU texture formats
* complex animation controller/state-machine assets
* audio streaming
* arbitrary scripting assets
* complex editor integration

The architecture should allow these later, but they should not complicate the first implementation.

---

## Rules of Thumb

* Gameplay code references asset IDs, not file paths.
* Systems request handles, not ownership of raw resources.
* The renderer owns GPU resources.
* The mesher never loads assets.
* The block atlas is built from finalized block texture references.
* Padding is mandatory before mipmap generation.
* Atlas UVs sample only the unpadded region.
* Failed hot reload keeps last known-good resources where possible.
* Resource changes must be versioned.
* Missing assets must be visible and diagnosable.

---
