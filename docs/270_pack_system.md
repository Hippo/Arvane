# Pack System

## Purpose

The pack system defines how Arvane discovers, validates, orders, and loads externally provided content.

A **pack** is a namespaced content bundle. Packs may provide assets, data definitions, world generation configuration, UI themes, shaders, sounds, and optionally native plugin code. The pack system exists so Arvane can support user content, server-required content, client-side visual overrides, and gameplay-modifying extensions without relying on hardcoded global asset paths.

The core rule is:

> A pack is the installable unit.  
> A plugin is optional executable code attached to a pack.

The engine should treat assets, data, and native code as separate classes of content even when they are distributed inside the same pack.

---

## Terminology

### Pack

A pack is an installable content unit with a manifest, identity, version, namespace ownership, and optional dependencies.

A pack may contain:

- Presentation assets
- Data definitions
- World generation configuration
- UI themes
- Shaders
- Sounds
- Native plugin code

### Asset Pack

An asset pack contains presentation resources such as textures, models, animations, fonts, sounds, shaders, and UI themes.

Asset packs are usually client-side and should not affect authoritative gameplay state.

### Data Pack

A data pack contains game definitions and configuration such as blocks, items, entities, biomes, dimensions, recipes, loot tables, tags, and world generation rules.

Data packs may affect authoritative gameplay state and therefore participate in registry construction and world compatibility checks.

### Plugin Pack

A plugin pack is a pack that includes native executable code through a dynamic library.

Native plugin code is privileged and must never be loaded implicitly merely because a pack exists on disk.

### Mod

“Mod” is a user-facing term for a pack that changes gameplay. Internally, gameplay-changing mods are still represented as packs.

### Namespace

A namespace is the prefix used in stable resource identifiers.

Examples:

```text
arvane:dirt
arvane:stone
better_oceans:kelp_forest
example_pack:blue_lamp
```

Namespaces prevent unrelated packs from colliding with each other.

### Resource ID

A resource ID is a stable namespaced identifier in the form:

```text
namespace:path
```

Examples:

```text
arvane:dirt
arvane:model/entity/pig
arvane:texture/block/stone
better_oceans:abyssal_plain
```

The meaning of the path is interpreted by the loading context. For example, `arvane:dirt` may be a block ID when loaded from block definitions, while `arvane:model/entity/pig` may be an asset reference.

### Pack Stack

The pack stack is the ordered list of enabled packs. Earlier packs provide base content. Later packs may add content or override eligible content from earlier packs.

The pack stack must be deterministic.

---

## Design Goals

The pack system should satisfy the following goals:

1. **Deterministic loading**

   The same enabled packs, versions, and load order must produce the same registry and resource resolution results.

2. **Stable namespaced identifiers**

   Content should be referenced by stable resource IDs, not by absolute file paths or runtime-generated numeric IDs.

3. **Clear separation between assets, data, and code**

   Assets may be reloadable. Data may affect registries and saves. Native code is privileged executable logic.

4. **Safe default behavior**

   Loading textures or data definitions must not imply loading native code.

5. **Save compatibility**

   Worlds must record the pack set required to interpret their saved content.

6. **Multiplayer compatibility**

   Servers must be able to declare required packs. Clients must be able to validate whether they have compatible content before joining.

7. **Good diagnostics**

   Pack loading failures must produce actionable errors that identify the pack, file, namespace, resource ID, and reason for failure.

8. **Minimal hardcoded content paths**

   Game systems should request resources through resource IDs and registries instead of directly reading from global asset directories.

---

## Non-Goals

The pack system does not require the first implementation to provide:

* Online pack distribution
* Automatic downloading from arbitrary servers
* Native plugin sandboxing
* Native plugin hot reload
* A scripting language
* A public marketplace
* Dependency solving from remote repositories

Those systems may be added later without changing the core pack model.

---

## Runtime Behavior

The runtime loads the enabled pack stack from `arvane_home/packs/enabled.yaml`.
If the enabled list is absent, `arvane_base` is the default stack. Pack loading
validates manifests, namespaces, dependency order, version constraints, native
metadata paths, and path traversal before content is used.

Assets resolve by resource ID through the ordered pack stack. Later packs have
higher priority for eligible asset overrides. Registry-affecting data is stricter:
duplicate definitions are rejected unless the overriding data explicitly declares
override intent.

World metadata records required pack IDs. Loading a locked world fails clearly
when a required pack is missing. Worlds do not lock against content hashes
because saved content resolves through stable string IDs. Multiplayer protocol
packets for full required-pack negotiation remain an integration point; that
flow may use richer version/hash metadata without changing world saves.

Native plugin declarations are manifest metadata only. Normal pack discovery and
content loading never load native libraries.

Built-in packs may also declare a compiled module in `builtin.module`. Built-in
modules are linked into the executable and registered by engine startup code;
they are not dynamic libraries and do not use the native plugin loader.

For the base pack, `builtin.module: "arv_base"` names the compiled provider for
base-game behavior. Runtime composition code registers that module into generic
registries before dependent registry construction or runtime startup. Generic
systems own registries, parsers, and loaders; `arv_base` only contributes
`arvane_base` behavior such as built-in game modes, base block/item behavior
IDs, base entity behavior IDs, the built-in player entity definition, starter
gameplay defaults, and command implementations.

---

## Pack Directory Layout

Packs live under `arvane_home/packs/`.

```text
arvane_home/
  packs/
    enabled.yaml

    arvane_base/
      pack.yaml
      assets/
        arvane/
          textures/
          models/
          animations/
          shaders/
          sounds/
          fonts/
          themes/
          ui/
      data/
        arvane/
          blocks/
          items/
          entities/
          biomes/
          dimensions/
          worldgen/
          recipes/
          loot_tables/
          tags/

    better_oceans/
      pack.yaml
      assets/
        better_oceans/
          textures/
          models/
          sounds/
      data/
        better_oceans/
          blocks/
          items/
          biomes/
          worldgen/

    example_plugin_pack/
      pack.yaml
      assets/
        example_plugin_pack/
      data/
        example_plugin_pack/
      native/
        linux-x86_64/
          libexample_plugin_pack.so
        windows-x86_64/
          example_plugin_pack.dll
        macos-arm64/
          libexample_plugin_pack.dylib
```

A pack has three major content roots:

```text
assets/  - presentation resources
data/    - game definitions and configuration
native/  - optional executable plugin code
```

These roots must not be treated as equivalent. Each root has different loading rules.

---

## Built-In Content

Arvane’s built-in content should be represented as a normal pack.

Recommended base pack:

```text
arvane_home/packs/arvane_base/
```

The base pack provides the `arvane` namespace.

Example manifest identity:

```yaml
id: "arvane_base"
display_name: "Arvane Base"
namespaces:
  - "arvane"
```

The base pack should be special only in that it is required by default. It should otherwise participate in the same pack loading, resource resolution, and validation rules as other packs.

This avoids a split model where built-in content uses one loader and external content uses another.

Compiled behavior for the base pack lives in `engine/arv_base/`. Data and assets
for the same pack live in `arvane_home/packs/arvane_base/`; C++ source code must
not be placed in the runtime pack directory.

---

## Enabled Pack List

The enabled pack list defines the local default pack stack.

```yaml
enabled:
  - "arvane_base"
  - "better_oceans"
  - "user_texture_overrides"
```

The order is meaningful.

Earlier packs are lower priority. Later packs are higher priority for eligible overrides.

For singleplayer worlds, the enabled pack list may be combined with the world’s required pack list. For multiplayer, the server’s required pack list is authoritative for gameplay-affecting content.

---

## Pack Manifest

Every pack must contain a `pack.yaml` manifest at its root.

Example:

```yaml
schema_version: 1

id: "better_oceans"
display_name: "Better Oceans"
version: "1.0.0"
description: "Adds deeper ocean biomes, ocean blocks, and world generation rules."

authors:
  - "ExampleAuthor"

namespaces:
  - "better_oceans"

side: "both"

engine:
  min_version: "0.1.0"

dependencies:
  - id: "arvane_base"
    version: ">=0.1.0"

load_after: []
load_before: []

capabilities:
  assets: true
  data: true
  native_plugin: false

builtin:
  module: "arv_base"
```

A plugin pack may also include native plugin metadata:

```yaml
schema_version: 1

id: "example_plugin_pack"
display_name: "Example Plugin Pack"
version: "1.0.0"

namespaces:
  - "example_plugin_pack"

side: "both"

capabilities:
  assets: true
  data: true
  native_plugin: true

native:
  api_version: 1
  entrypoint: "arvane_plugin_main"
  libraries:
    linux-x86_64: "native/linux-x86_64/libexample_plugin_pack.so"
    windows-x86_64: "native/windows-x86_64/example_plugin_pack.dll"
    macos-arm64: "native/macos-arm64/libexample_plugin_pack.dylib"
```

---

## Manifest Fields

### `schema_version`

The version of the pack manifest format.

This is not the same as the pack’s own version.

### `id`

The stable ID of the pack.

Pack IDs should be lowercase, stable, and filesystem-safe.

Recommended format:

```text
[a-z0-9_][a-z0-9_.-]*
```

Examples:

```text
arvane_base
better_oceans
example_pack
```

### `display_name`

Human-readable name shown in menus and diagnostics.

### `version`

The pack’s version.

Semantic versioning is recommended, but the engine should treat the manifest schema version and the pack content version as separate concepts.

### `description`

Optional human-readable description.

### `authors`

Optional list of author names.

### `namespaces`

List of namespaces the pack is allowed to provide.

For normal third-party packs, the primary namespace should usually match the pack ID.

The base pack may be an exception:

```yaml
id: "arvane_base"
namespaces:
  - "arvane"
```

A pack must not define content outside its declared namespaces unless explicitly allowed by an override mechanism.

### `side`

Defines where the pack is intended to load.

Allowed values:

```text
client
server
both
```

Examples:

| Pack Type              | Typical Side       |
| ---------------------- | ------------------ |
| Texture pack           | `client`           |
| UI theme pack          | `client`           |
| Shader pack            | `client`           |
| Worldgen pack          | `server` or `both` |
| Block/item pack        | `both`             |
| Native gameplay plugin | `server` or `both` |

### `engine`

Defines engine compatibility constraints.

Example:

```yaml
engine:
  min_version: "0.1.0"
  max_version: "0.2.x"
```

The engine should reject packs that declare incompatible engine requirements.

### `dependencies`

Required packs that must be present before this pack can load.

Example:

```yaml
dependencies:
  - id: "arvane_base"
    version: ">=0.1.0"
  - id: "better_core"
    version: ">=2.0.0"
```

### `load_after` and `load_before`

Ordering hints used when two packs do not have a hard dependency relationship.

These fields should not replace actual dependencies. If a pack requires another pack’s content to function, it should use `dependencies`.

### `capabilities`

Declares what categories of content the pack contains.

Example:

```yaml
capabilities:
  assets: true
  data: true
  native_plugin: false
```

Capabilities are descriptive and should be validated against the pack contents where possible.

### `native`

Describes optional native plugin code.

This section is only valid when:

```yaml
capabilities:
  native_plugin: true
```

The presence of this section does not mean the native plugin should be loaded automatically.

### `builtin`

Describes optional compiled behavior that is already linked into the executable.
This is for built-in packs such as `arvane_base`, whose gameplay behavior is
provided by an engine module rather than a dynamic library.

Example:

```yaml
builtin:
  module: "arv_base"
```

The presence of this section does not dynamically load code. It declares that
the executable is expected to provide and register the named module.

Startup treats missing required handlers from a built-in module as configuration
errors. For example, if base entity data references `arvane:wander`, the base
entity goal registry must have that handler registered before entity registry
construction succeeds. Block and item data that declare `behavior` IDs are
validated against the block/item behavior registries before their registries are
built. Likewise, configured server behaviors such as
`arvane:block_break/mineable_tags` must be registered before server startup.

---

## Resource IDs

Arvane resource IDs use the format:

```text
namespace:path
```

Examples:

```text
arvane:dirt
arvane:stone
arvane:model/entity/pig
better_oceans:kelp_forest
better_oceans:coral_lamp
```

The namespace identifies ownership. The path identifies a resource within that namespace.

Resource IDs should be stable across engine runs. Runtime numeric IDs may be generated for performance, but they must not be the authoritative identity stored in content files or saves.

---

## Namespace Ownership

A pack may only define new resources inside namespaces listed in its manifest.

Example:

```yaml
id: "better_oceans"
namespaces:
  - "better_oceans"
```

This pack may define:

```text
better_oceans:kelp_forest
better_oceans:coral_lamp
better_oceans:deep_ocean_noise
```

It may not silently define:

```text
arvane:dirt
other_pack:something
```

Overriding or patching another namespace must require explicit override or patch metadata.

This prevents accidental content replacement and namespace squatting.

---

## Asset Content

Assets are presentation resources.

Common asset directories:

```text
assets/<namespace>/textures/
assets/<namespace>/models/
assets/<namespace>/animations/
assets/<namespace>/shaders/
assets/<namespace>/sounds/
assets/<namespace>/fonts/
assets/<namespace>/themes/
assets/<namespace>/ui/
assets/<namespace>/particles/
```

Examples:

```text
assets/arvane/textures/block/dirt.png
assets/arvane/models/entity/pig.json
assets/arvane/animations/entity/pig.json
assets/arvane/shaders/world/sky.vert
assets/arvane/sounds/block/stone_step.ogg
```

Assets should be resolved through the pack stack.

Game systems should request assets using resource IDs rather than hardcoded file paths.

Example:

```text
arvane:texture/block/dirt
arvane:model/entity/pig
arvane:animation/entity/pig
```

The exact mapping from a resource ID to a file path is owned by the asset loader.

---

## Data Content

Data is authoritative game content and configuration.

Common data directories:

```text
data/<namespace>/blocks/
data/<namespace>/items/
data/<namespace>/entities/
data/<namespace>/biomes/
data/<namespace>/dimensions/
data/<namespace>/worldgen/
data/<namespace>/recipes/
data/<namespace>/loot_tables/
data/<namespace>/tags/
data/<namespace>/commands/
```

Examples:

```text
data/arvane/blocks/dirt.yaml
data/arvane/items/stone_pickaxe.yaml
data/better_oceans/biomes/kelp_forest.yaml
data/better_oceans/worldgen/noise/abyssal_depth.yaml
data/better_oceans/worldgen/features/coral_patch.yaml
```

Data definitions may affect registries, world generation, saves, and gameplay logic. For that reason, data definitions need stricter validation than assets.

---

## Asset Resolution Rules

Assets resolve through the pack stack.

Given this pack order:

```text
1. arvane_base
2. better_oceans
3. user_texture_overrides
```

A request for:

```text
arvane:texture/block/dirt
```

is resolved by checking packs from highest priority to lowest priority.

Conceptually:

```text
user_texture_overrides
better_oceans
arvane_base
```

The first matching asset wins.

This makes texture and model overrides straightforward.

Asset overrides are allowed by default because assets are presentation resources. Invalid asset overrides should produce warnings or errors depending on severity, but they should not silently corrupt registries or saves.

---

## Data Merge Rules

Data definitions must not rely only on file shadowing.

The pack system should distinguish between:

1. Additive definitions
2. Explicit overrides
3. Patches

### Additive Definitions

Additive definitions introduce new content.

Example:

```text
better_oceans:coral_lamp
better_oceans:kelp_forest
better_oceans:abyssal_plain
```

Additive definitions are safe when they stay inside the pack’s own namespace.

### Explicit Overrides

An override replaces an existing definition.

Overrides must be explicit.

A pack should not accidentally replace another pack’s block, item, biome, or entity merely by defining the same resource ID.

Example conceptual metadata:

```yaml
operation: "override"
target: "arvane:dirt"
```

The exact schema may vary by data type, but the intent must be explicit.

### Patches

A patch modifies selected fields of an existing definition.

Example conceptual metadata:

```yaml
operation: "patch"
target: "arvane:dirt"

patch:
  friction: 0.72
```

Patches are useful for compatibility packs and small changes, but they require careful validation. Patch support can be added after additive definitions and explicit overrides are stable.

---

## Registry Construction

Registry-affecting data should be loaded in a deterministic pipeline.

Conceptual order:

1. Discover enabled packs.
2. Read and validate pack manifests.
3. Resolve dependencies.
4. Compute pack load order.
5. Scan declared asset and data roots.
6. Validate data files against their schemas.
7. Apply additive definitions.
8. Apply explicit overrides.
9. Apply patches.
10. Build runtime registries.
11. Assign runtime IDs.
12. Report diagnostics.

Stable resource IDs are the source of truth.

Runtime IDs are derived implementation details used for fast lookup, networking, meshing, simulation, and rendering. Runtime IDs must be reproducible for a given content set, but saves should not depend on them as permanent identities.

---

## Load Order

The pack load order must be deterministic.

Inputs to load order include:

* The enabled pack list
* Required world packs
* Required server packs
* Pack dependencies
* `load_after` hints
* `load_before` hints

Hard dependencies take precedence over ordering hints.

A dependency cycle is an error.

Example:

```text
arvane_base
better_oceans
better_oceans_compat
user_texture_overrides
```

Later packs have higher priority for eligible asset overrides and explicit data modifications.

---

## Dependency Rules

A pack should declare another pack as a dependency when it references that pack’s data or assets as required content.

Example:

```yaml
dependencies:
  - id: "arvane_base"
    version: ">=0.1.0"
```

If a required dependency is missing or incompatible, the pack must not load.

Optional integrations should not be modeled as required dependencies. They should be modeled as optional compatibility behavior or separate compatibility packs.

Example:

```text
better_oceans
better_oceans_extra_blocks_compat
```

This keeps the base pack usable without forcing unrelated dependencies.

---

## Side Rules

Packs may be client-side, server-side, or both.

### Client-Side Packs

Client-side packs affect local presentation.

Examples:

* Texture packs
* Shader packs
* UI themes
* Font packs
* Sound replacement packs

Client-side packs must not alter authoritative gameplay registries.

### Server-Side Packs

Server-side packs affect authoritative behavior.

Examples:

* World generation packs
* Loot table packs
* Server-only native plugins
* Permission or command configuration packs

Server-side packs may not require the client to load visual assets unless they introduce content that the client must render or understand.

### Both-Side Packs

Both-side packs define content that needs to exist on both server and client.

Examples:

* Blocks
* Items
* Entities
* Biomes with custom presentation
* Dimensions with client-visible rules

For multiplayer, both-side packs are usually part of the server-required content set.

---

## Multiplayer Pack Model

The server is authoritative for gameplay-affecting content.

A multiplayer server should declare the set of packs required for the client to join and play correctly.

Required pack metadata should include enough information for the client to validate compatibility, such as:

* Pack ID
* Version
* Content hash when available
* Required side
* Required namespaces
* Whether native client code is required

A client may have additional local client-side packs, but those packs must not alter authoritative registries or gameplay state.

Examples of valid local client-side overrides:

* Higher-resolution textures
* Different UI theme
* Different sounds
* Client-only shaders

Examples of content that must match the server:

* Block definitions
* Item definitions
* Entity definitions
* Biome definitions
* Dimension definitions
* Registry-affecting tags
* World generation definitions relevant to streamed/generated content

If the client is missing required content, it should fail with a clear message rather than entering a partially valid world.

---

## Server-Required Packs

A server-required pack is a pack that the client must have in a compatible form to join the server.

Examples:

```text
arvane_base
better_oceans
server_blocks
```

The client should validate the required pack set before completing the join flow.

If automatic pack download is supported later, it must still respect native-code safety rules. Native code should not be downloaded and executed silently.

---

## Local Client Packs

Local client packs are controlled by the player and apply only to local presentation.

Examples:

```text
faithful_style_textures
dark_ui_theme
high_contrast_crosshair
```

Local client packs must not introduce or modify server-authoritative gameplay definitions while connected to a multiplayer server unless the server explicitly allows that content.

In multiplayer, local client packs are layered after server-required client assets for eligible presentation overrides.

---

## World Pack Lock

A world should record the pack set required to interpret its saved content.

Example:

```yaml
packs:
  required:
    - "arvane_base"
    - "better_oceans"

content_lock:
  enabled: true
```

The world pack lock protects saves from being loaded with missing content.
It records pack IDs only. Pack versions and hashes are intentionally not part of
world load validation because saved chunks, entities, and palettes resolve from
stable string IDs.

If a world references:

```text
better_oceans:coral_lamp
```

and the `better_oceans` pack is unavailable, the engine should not silently replace that content with unrelated blocks.

Valid behaviors include:

* Refuse normal load with a clear error.
* Enter an explicit recovery mode.
* Substitute a visible missing-content placeholder only when the user intentionally chooses recovery.

Silent data loss is not acceptable.

---

## Save Compatibility

Saved worlds should store stable resource IDs or palettes that resolve to stable resource IDs.

They should not permanently rely on runtime numeric IDs.

Runtime IDs may be used inside a running engine instance for performance, but they are reconstructed from the loaded pack stack.

When loading a world, the engine should validate that all required stable IDs can be resolved.

If content is missing, diagnostics should identify:

* Missing resource ID
* Owning pack if known
* Affected world
* Affected chunk, entity, or data file when known

---

## Native Plugin Code

Native plugin code is privileged executable code.

It must be handled separately from assets and data.

A pack containing native code should declare:

```yaml
capabilities:
  native_plugin: true

native:
  api_version: 1
  entrypoint: "arvane_plugin_main"
  libraries:
    linux-x86_64: "native/linux-x86_64/libexample.so"
```

The engine must not load native plugin code just because this metadata exists.

Native plugins require explicit enablement.

Example policy:

```yaml
native_plugins:
  enabled: true
  allowed_packs:
    - "example_plugin_pack"
```

Recommended rules:

1. Native plugins are disabled by default.
2. Native plugins must be explicitly allowed.
3. Native plugin paths must remain inside the pack root.
4. Native plugins must declare an API version.
5. Native plugins should use a stable C ABI boundary.
6. Native plugins should not expose C++ STL types across the ABI boundary.
7. Native plugins should not be hot-unloaded during normal gameplay.
8. Native plugins should receive engine access through narrow, versioned API tables.

Conceptual entrypoint:

```cpp
extern "C" ARV_PLUGIN_EXPORT bool arvane_plugin_main(const ArvPluginAPI* api);
```

The exact API is defined by the plugin system, not by the pack manifest.

---

## Native Plugin Safety

Native plugins have the same privileges as the process that loads them. They can crash the engine, corrupt memory, access files, and execute arbitrary code.

Therefore:

* Native plugin loading must be opt-in.
* Multiplayer servers must not cause clients to execute native code silently.
* Native plugin errors must identify the owning pack.
* Native plugin ABI mismatches must fail cleanly.
* Native plugin loading should be auditable through logs and UI.

Native code should be treated as trusted code from a trusted source, not as ordinary user content.

---

## Shader Packs

Shaders are assets, but they need stricter handling than simple textures.

Shader packs may affect rendering behavior and GPU stability, but they should not affect authoritative gameplay state.

Shader loading should provide:

* Clear compile errors
* Clear link errors
* Identification of the owning pack
* Fallback behavior where possible
* A way to disable broken shader packs

Shader packs are normally client-side.

---

## Hot Reloading

Not all pack content should support the same reload behavior.

### Generally Reloadable

These may be reloadable during development or normal client use:

* Textures
* Models
* Animations
* Sounds
* Fonts
* UI themes
* Shaders, with fallback handling

### Conditionally Reloadable

These may be reloadable in controlled development contexts:

* Recipes
* Loot tables
* Tags
* Some UI definitions
* Some non-registry gameplay configuration

### Not Normally Hot-Reloadable

These should not be hot-reloaded in active gameplay without a deliberate reload flow:

* Block registries
* Item registries
* Entity type registries
* Biome registries
* Dimension registries
* World generation definitions for active worlds
* Native plugins

Registry-affecting data should generally require a world reload, server restart, or explicit development-mode reload.

---

## File Path Safety

Pack loading must treat pack files as untrusted input.

The resolver must prevent:

* Path traversal outside the pack root
* Absolute path escapes
* Invalid namespace ownership
* Invalid file extensions for a loader
* Unexpected binary formats
* Duplicate definitions without explicit override intent

Resource references should not be allowed to directly escape into arbitrary filesystem paths.

Invalid examples:

```text
../../outside_file.png
/etc/passwd
C:\Users\SomeUser\secret.txt
```

The pack root is the maximum filesystem authority for ordinary pack content.

---

## Diagnostics

Pack loading diagnostics should be precise.

Good error messages include:

* Pack ID
* Pack version when known
* File path within the pack
* Resource ID
* Loader phase
* Human-readable reason
* Dependency chain when relevant

Example:

```text
Failed to load block definition.

Pack: better_oceans 1.0.0
File: data/better_oceans/blocks/coral_lamp.yaml
Resource: better_oceans:coral_lamp
Reason: Unknown texture reference better_oceans:texture/block/coral_lamp_side
```

Diagnostics should avoid vague failures such as:

```text
Failed to load asset.
Invalid data.
Could not parse file.
```

---

## Pack Validation

A pack should be validated before its content participates in registries.

Validation should check:

* Manifest exists.
* Manifest schema version is supported.
* Pack ID is valid.
* Version is valid.
* Side value is valid.
* Declared namespaces are valid.
* Required dependencies exist.
* Dependency versions are compatible.
* Data files are inside declared namespaces.
* Native library paths stay inside the pack root.
* Resource IDs are syntactically valid.
* Data schemas are valid.
* Duplicate definitions are intentional.

Validation failure should prevent the invalid pack from loading.

For development tools, warnings may be collected and displayed without aborting if the pack remains semantically safe.

---

## Conflict Handling

Conflicts should be handled differently for assets and data.

### Asset Conflicts

Asset conflicts are normal.

If two enabled packs provide the same asset resource ID, the later pack wins.

Example:

```text
arvane_base provides arvane:texture/block/dirt
user_texture_overrides provides arvane:texture/block/dirt
```

The override is valid.

### Data Conflicts

Data conflicts require explicit intent.

If two packs define the same block, item, entity, biome, or dimension ID without override metadata, the engine should report an error.

This prevents accidental registry changes.

---

## Content Hashes

A pack may have a content hash used for multiplayer compatibility and diagnostics.

The content hash should represent the meaningful contents of the pack, not incidental filesystem metadata.

Hash inputs may include:

* Manifest
* Data files
* Required client assets
* Native plugin metadata when relevant

Hash inputs should exclude:

* Temporary files
* Editor backup files
* Logs
* Local cache files
* Generated build artifacts not part of the pack

Multiplayer servers may require exact content hashes for registry-affecting packs.

---

## Caches

The engine may cache processed pack data.

Examples:

* Parsed manifests
* Asset indexes
* Texture atlases
* Compiled shader metadata
* Model import results
* Registry construction intermediates

Caches must be invalidated when relevant pack contents change.

Caches are derived data. They must not become the source of truth.

---

## Interaction With Registries

Packs are the source of stable definitions. Registries are the runtime representation of those definitions.

Examples:

```text
Pack data:      arvane:dirt
Runtime block:  BlockID / BlockStateID assigned during registry construction
Save data:      stable resource ID or palette resolving to stable resource ID
```

The registry layer may use dense runtime IDs for performance, but the pack layer owns stable identity.

This is especially important for:

* Blocks
* Block states
* Items
* Entity types
* Biomes
* Dimensions
* Fluids
* Commands
* Permissions
* Tags

---

## Interaction With World Generation

World generation configuration belongs in data packs.

Recommended directories:

```text
data/<namespace>/worldgen/noise/
data/<namespace>/worldgen/features/
data/<namespace>/worldgen/carvers/
data/<namespace>/worldgen/biome_sources/
data/<namespace>/worldgen/terrain/
data/<namespace>/biomes/
data/<namespace>/dimensions/
```

World generation packs are authoritative. In multiplayer, the server owns world generation.

Clients may need matching biome, block, and dimension presentation data, but clients should not be authoritative over generated terrain.

A world should record the IDs of worldgen-affecting packs that were active when it was created or last loaded.

---

## Interaction With UI Themes

UI themes are assets.

Recommended location:

```text
assets/<namespace>/themes/
```

A local client may apply UI theme packs without affecting server compatibility.

The selected theme may be referenced by resource ID:

```text
arvane:theme/default
example_pack:theme/dark_blue
```

Theme selection should be user configuration, not world state, unless a server explicitly provides a required UI for a specific gameplay mode.

---

## Pack Distribution

The pack system does not require a distribution mechanism.

Packs may be installed manually, bundled with the game, provided by servers, or managed by future tooling.

Regardless of distribution source, the same validation, load order, namespace, and native-code rules apply.

---

## Recommended Naming in Code

Use pack terminology for the content bundle system:

```text
Pack
PackID
PackManifest
PackManager
PackRegistry
PackSource
PackLoadOrder
PackDependency
PackNamespace
PackIndex
PackDiagnostic
```

Use plugin terminology only for executable extension code:

```text
Plugin
NativePlugin
PluginAPI
PluginHost
PluginHandle
PluginEntrypoint
```

Avoid using `Mod` as the core engine type. It is useful in user-facing UI, but too vague for engine architecture.

---

## Summary

The pack system should model Arvane content as deterministic, namespaced, ordered content bundles.

The core structure is:

```text
Pack
  assets/  -> presentation resources
  data/    -> authoritative definitions and configuration
  native/  -> optional privileged executable code
```

Assets may be overridden through pack order. Data definitions require stricter merge semantics. Native code requires explicit enablement.

This gives Arvane one unified content pipeline for built-in content, user asset packs, server-required content, world generation packs, and future native plugin extensions while preserving save safety, multiplayer compatibility, and clear security boundaries.
