# Plugin API

## Purpose

The plugin API defines how native executable code extends Arvane.

Arvane’s content model is pack-first:

> A pack is the installable unit.
> A plugin is optional executable code attached to a pack.

Packs provide assets, data definitions, world generation configuration, UI themes, shaders, sounds, and optional native code. The plugin API only covers the native code portion.

Most modded content should be expressed through pack data. Native plugin code should be used when data definitions need custom behavior, services, event handling, command execution, or other logic that cannot be represented safely as static data.

---

## Core Rules

1. **Packs own content identity.**

   Blocks, items, entities, biomes, dimensions, commands, tags, recipes, loot tables, and world generation definitions are identified by stable namespaced resource IDs from pack data.

2. **Plugins provide behavior.**

   Native plugins register handlers, services, callbacks, codecs, systems, and command executors. They should not be the primary way to define ordinary blocks, items, or worldgen entries.

3. **Native code is privileged.**

   Native plugins can crash the process and access host resources. They must be explicitly enabled and must never be loaded merely because a pack exists on disk.

4. **All plugin access goes through versioned APIs.**

   Plugins must not directly mutate engine internals, access raw chunks, access the ECS registry, or assume memory layout of engine types.

5. **Threading is explicit.**

   Synchronous work runs on the relevant engine tick/simulation context. Asynchronous work runs on worker threads and may only use thread-safe APIs or immutable snapshots.

---

## Design Goals

The plugin API should provide:

* stable native extension points
* deterministic startup and shutdown
* explicit pack integration
* clear namespace ownership
* data-first content authoring
* server-authoritative gameplay compatibility
* safe threading rules
* useful diagnostics
* service discovery between plugins
* clean separation between assets, data, and code

The API should avoid:

* hidden global state
* direct engine memory access
* runtime registry mutation after startup
* arbitrary scripting as a first implementation requirement
* implicit native code loading
* plugin behavior that depends on load order accidents

---

## Terminology

### Pack

A pack is an installable namespaced content bundle with a manifest.

A pack may contain:

```text
assets/  - presentation resources
data/    - authoritative definitions and configuration
native/  - optional executable plugin code
```

### Plugin Pack

A plugin pack is a pack that declares native executable code in its manifest.

A plugin pack may also include ordinary assets and data.

### Native Plugin

A native plugin is compiled code loaded from a plugin pack.

Examples:

```text
native/linux-x86_64/libexample_pack.so
native/windows-x86_64/example_pack.dll
native/macos-arm64/libexample_pack.dylib
native/macos-x86_64/libexample_pack.dylib
```

### Built-In Module

A built-in module is compiled into the executable and registered by engine startup code.

The base game behavior should use this model:

```yaml
builtin:
  module: "arv_base"
```

The `arv_base` module provides compiled behavior for the `arvane_base` pack. The pack’s data and assets live under `arvane_home/packs/arvane_base/`; the C++ source code lives under `engine/arv_base/`.

### Resource ID

A resource ID is a stable namespaced identifier:

```text
namespace:path
```

Examples:

```text
arvane:dirt
arvane:model/entity/pig
better_oceans:kelp_forest
example_pack:glowing_block_behavior
```

Plugins must use resource IDs for engine-facing identities.

### Plugin Host

The plugin host is the controlled API table given to native plugin code.

Plugins do not access engine singletons directly.

### Service

A service is a plugin-provided API that other plugins can discover and use through the engine’s service registry.

Examples:

```text
economy:accounts
claims:regions
permissions:groups
quests:quest_manager
```

---

## Relationship to the Pack System

Native plugins are attached to packs through `pack.yaml`.

Example plugin pack manifest:

```yaml
schema_version: 1

id: "example_plugin_pack"
display_name: "Example Plugin Pack"
version: "1.0.0"

namespaces:
  - "example_plugin_pack"

side: "server"

engine:
  min_version: "0.1.0"

dependencies:
  - id: "arvane_base"
    version: ">=0.1.0"

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
    macos-x86_64: "native/macos-x86_64/libexample_plugin_pack.dylib"
```

The presence of `native` metadata does not automatically load the plugin.

Native plugin loading is controlled by runtime policy in config. Generated
configs enable native-plugin loading by default, but native code still runs only
for packs listed in `native_plugins.allowed_packs`:

```yaml
native_plugins:
  enabled: true
  allowed_packs:
    - "example_plugin_pack"
```

Rules:

* normal pack discovery must not load native libraries
* native plugin paths must remain inside the pack root
* the plugin API version must match a supported engine API version
* plugin diagnostics must identify the owning pack
* multiplayer servers must not cause clients to execute native code silently
* client-side native plugins require explicit local trust through the same pack allowlist

---

## Content Authoring Model

### Preferred Model

Plugin packs should define content through pack data:

```text
data/blocks.yaml
data/items.yaml
data/entities.yaml
data/worldgen/features/crystal_cluster.yaml
data/commands.yaml
```

Native plugin code registers behavior referenced by that data.

Example block data:

```yaml
id: example_plugin_pack:glowing_block
version: 1

display_name: "Glowing Block"

behavior: example_plugin_pack:glowing_block_behavior

presentation:
  model: example_plugin_pack:model/block/glowing_block
```

Example native registration:

```cpp
api->registries->register_block_behavior(
    api->registries_ctx,
    arv_cstr("example_plugin_pack:glowing_block_behavior")
);
```

This keeps the registry source of truth in pack data while still allowing native behavior.

### Avoided Model

Plugins should not normally do this:

```cpp
host->registry->register_block(...);
host->registry->register_item(...);
```

Direct runtime content registration makes pack validation, save compatibility, multiplayer compatibility, and deterministic registry construction harder.

The engine may keep limited direct registration hooks for built-in bootstrap cases, tests, or development tooling, but the stable modding model should be data-first.

---

## Native ABI Boundary

Native plugins should use a stable C ABI boundary.

The SDK also provides first-class C++ wrappers split by API domain.
For example, `arv::Event` and `arv::Events` live in `<arv/events.hpp>`,
`arv::Services` lives in `<arv/services.hpp>`, and `arv::Commands` lives in
`<arv/commands.hpp>`. `<arv/plugin.hpp>` is the aggregate facade for plugin
entrypoints and exposes `arv::Plugin`.

C++ plugins should use `arv::Plugin`, `arv::Logger`, `arv::Services`,
`arv::Commands`, and the other `arv::` wrapper types instead of calling raw C
tables directly. The actual binary interface remains C and should avoid:

* C++ standard library types
* exceptions crossing the ABI boundary
* RTTI assumptions
* engine-owned object layouts
* raw ownership transfer without explicit destroy functions

Conceptual entrypoint:

```cpp
extern "C" ARV_EXPORT
bool arvane_plugin_main(const ArvAPI* api);
```

The plugin returns `true` if initialization succeeds.

The plugin returns `false` if it cannot initialize. The engine should then disable that plugin and report diagnostics.

Implemented ABI surface:

* The C ABI lives in the standalone SDK at `sdk/arvane_plugin/include/arv/api.h`.
* Plugins include SDK headers and link only the `Arvane::SDK` CMake target.
* The engine implements the ABI in `engine/arv_plugin/` and passes function tables to plugins at load time.
* API version `1` is exposed as `ARV_API_VERSION`.
* `pack.yaml` native metadata must declare `api_version: 1`, a C-symbol entrypoint, and a library for the current platform key.
* Native libraries are loaded only when `native_plugins.enabled` is true and the owning pack is listed in `native_plugins.allowed_packs`.
* Implemented API tables include context, logger, behavior registration, command executor registration, event subscription, sync/async scheduling, service publish/lookup, data resource read/debug path, structured data documents, codec-backed config, scoped persistent data, basic world/entity access, network messaging, permissions, rich text helpers, regions, game modes, player selection queries, item stack data, and metrics counters.
* APIs may return `Unsupported` when the corresponding runtime context or engine extension point is not available.

Plugin author build surface:

```cmake
find_package(ArvaneSDK REQUIRED)

add_library(example_plugin SHARED
    src/example_plugin.cpp
)

target_link_libraries(example_plugin
    PRIVATE
        Arvane::SDK
)
```

`Arvane::SDK` provides include paths, compile features, and compiled C/C++
wrapper code for `<arv/*.h>` and `<arv/*.hpp>`. It must not pull in engine
link libraries. Shared standalone headers exported by engine modules, such as
`arv/io/codec.hpp`, may be installed with the SDK when they do not expose
runtime engine internals.

C++ plugin entrypoint:

```cpp
#include <arv/plugin.hpp>

ARV_MAIN(plugin) {
    plugin.logger().info("Plugin loaded.");
    return true;
}
```

The `ARV_MAIN` macro exports `arvane_plugin_main`, validates `ARV_API_VERSION`,
and constructs the `arv::Plugin` wrapper before calling the user body.

---

## Plugin API Table

The engine exposes functionality through versioned SDK API tables.

Conceptual shape:

```cpp
typedef struct ArvAPI {
    uint32_t api_version;

    void* context_ctx;
    const ArvContextAPI* context;

    void* registries_ctx;
    const ArvRegistryAPI* registries;

    void* scheduler_ctx;
    const ArvSchedulerAPI* scheduler;

    void* commands_ctx;
    const ArvCommandAPI* commands;

    void* services_ctx;
    const ArvServiceAPI* services;

    void* events_ctx;
    const ArvEventAPI* events;

    void* logger_ctx;
    const ArvLoggerAPI* logger;
};
```

Every sub-API call receives the matching opaque context pointer from the root
table. Plugins must treat those pointers as engine-owned and valid only for the
plugin lifetime.

---

## Plugin Context API

The context API exposes metadata about the currently loaded plugin.

SDK ABI shape:

```cpp
typedef struct ArvContextAPI {
    ArvStringView (*pack_id)(void* ctx);
    ArvStringView (*pack_version)(void* ctx);
    ArvStringView (*side)(void* ctx);
    uint32_t (*namespace_count)(void* ctx);
    ArvStringView (*namespace_at)(void* ctx, uint32_t index);
    bool (*owns_namespace)(void* ctx, ArvStringView name_space);
} ArvContextAPI;
```

Rules:

* a plugin may only register behavior and services under namespaces owned by its pack unless an explicit override or extension rule allows otherwise
* diagnostics should include the current pack ID
* plugins should not infer their pack path and read files directly; use `ResourceAPI` or `ConfigAPI`

---

## Lifecycle

Plugin lifecycle is tied to the resolved pack stack.

### Startup Order

1. Discover enabled packs.
2. Read and validate pack manifests.
3. Resolve dependencies and load order.
4. Validate native plugin metadata.
5. Apply native plugin enablement policy.
6. Load built-in modules and explicitly enabled native plugin libraries.
7. Validate plugin ABI versions.
8. Call plugin entrypoints.
9. Plugins register native handlers, codecs, services, command executors, and event subscriptions.
10. Pack data is loaded and validated.
11. Runtime registries are constructed.
12. Worlds, servers, and clients start.
13. Plugin runtime jobs and events become active.

This order allows pack data to reference native behavior IDs that plugins registered before registry construction.

### Shutdown Order

1. Stop accepting new plugin jobs.
2. Cancel scheduled plugin jobs owned by shutting-down plugins.
3. Dispatch plugin shutdown events.
4. Unregister plugin services.
5. Release plugin-owned resources.
6. Unload native libraries only when safe.

Hot unloading native plugins during active gameplay is not a required feature. The safe default is to keep native plugins loaded until process shutdown or server shutdown.

---

## Lifecycle Phases

The engine should expose lifecycle phases clearly.

| Phase                 | Allowed Plugin Work                                                         |
| --------------------- | --------------------------------------------------------------------------- |
| Native load           | ABI validation and entrypoint call                                          |
| Registration          | Register handlers, codecs, services, event subscriptions, command executors |
| Data loading          | Engine loads pack data and validates references                             |
| Registry construction | Engine builds runtime registries                                            |
| Runtime               | Events, scheduled jobs, world/entity operations                             |
| Shutdown              | Cleanup only                                                                |

Plugins must not mutate registry-affecting content after registries are locked.

---

## Registry API

The registry API is for registering native behavior used by data-driven definitions.

SDK ABI shape:

```cpp
typedef struct ArvRegistryAPI {
    ArvResult (*register_block_behavior)(void* ctx, ArvResourceID id);
    ArvResult (*register_item_behavior)(void* ctx, ArvResourceID id);
    ArvResult (*register_entity_goal)(void* ctx, ArvResourceID id);
    ArvResult (*register_entity_behavior)(void* ctx, ArvResourceID id);
    ArvResult (*register_component_codec)(void* ctx, ArvResourceID id);
    ArvResult (*register_loot_function)(void* ctx, ArvResourceID id);
    ArvResult (*register_recipe_type)(void* ctx, ArvResourceID id);
} ArvRegistryAPI;
```

Rules:

* behavior IDs must be namespaced
* registrations must happen before registry construction
* registration must fail on duplicate IDs unless the engine has explicit override semantics
* data files reference these behavior IDs by resource ID
* runtime numeric IDs are implementation details and must not be stored by plugins as stable identities

Example:

```yaml
id: example_plugin_pack:wand
version: 1

behavior: example_plugin_pack:wand_use
```

```cpp
api->registries->register_item_behavior(
    api->registries_ctx,
    arv_cstr("example_plugin_pack:wand_use")
);
```

---

## Resource API

The resource API resolves assets and data through the pack stack.

Conceptual shape:

```cpp
struct ResourceAPI {
    bool exists(ResourceID id, ResourceKind kind);
    ResourceBytes read(ResourceID id, ResourceKind kind);
    ResourcePath debug_resolved_path(ResourceID id, ResourceKind kind);
};
```

Rules:

* plugins should use resource IDs, not hardcoded paths
* resource access must respect pack stack resolution rules
* path traversal outside a pack root must be impossible
* debug paths are for diagnostics only and should not become stable identifiers
* asset access does not imply permission to mutate registries

---

## World API

The world API provides controlled world access.

Conceptual shape:

```cpp
struct Location {
    float x;
    float y;
    float z;
    float yaw_radians;
    float pitch_radians;
    optional<WorldID> world;
};

struct WorldAPI {
    BlockStateID get_block(WorldID world, BlockPos pos);
    bool set_block(WorldID world, BlockPos pos, BlockStateID state);
    Location get_world_spawn(WorldID world);
    bool set_world_spawn(WorldID world, Location spawn);

    WorldSnapshotID create_snapshot(WorldID world, SnapshotRegion region);
    BlockStateID snapshot_get_block(WorldSnapshotID snapshot, BlockPos pos);

    TickIndex current_tick(WorldID world);
    WorldSeed world_seed(WorldID world);
};
```

Rules:

* live world mutation must occur on the sync simulation context
* async jobs must not access live chunks directly
* async jobs may read immutable snapshots or explicitly thread-safe query APIs
* plugins must not store raw pointers to chunks, sections, block arrays, or lighting buffers
* block and biome identities should be resolved through registries and resource IDs
* world writes may be queued by the engine even when exposed as simple calls

---

## Player API

The player API exposes controlled player-scoped state without exposing
`PlayerState` layout.

SDK ABI shape:

```cpp
typedef struct ArvPlayerSelection {
    uint32_t flags;
    ArvWorldHandle world;
    ArvBlockPos a;
    ArvBlockPos b;
} ArvPlayerSelection;

typedef struct ArvPlayersAPI {
    ArvResult (*current_selection)(
        void* ctx,
        ArvPlayerID player,
        ArvPlayerSelection* out_selection);
} ArvPlayersAPI;
```

Rules:

* calls that inspect live player state must occur on the sync simulation context
* absent selection fields are represented by flags rather than sentinel block positions
* plugins should treat the selection as current gameplay state, not as plugin-owned persistent data

---

## Item Stack Data API

The item stack data API exposes Minecraft-NBT-style per-stack structured data
for player-visible stacks. V1 covers selected hotbar, inventory slot, and
cursor stacks only.

SDK ABI shape:

```cpp
typedef enum ArvItemStackLocationKind {
    ARV_ITEM_STACK_SELECTED_HOTBAR,
    ARV_ITEM_STACK_INVENTORY_SLOT,
    ARV_ITEM_STACK_CURSOR
} ArvItemStackLocationKind;

typedef struct ArvItemStackLocation {
    ArvPlayerID player;
    ArvItemStackLocationKind kind;
    uint32_t slot_index;
} ArvItemStackLocation;

typedef struct ArvItemStackInfo {
    bool present;
    uint32_t item_id;
    ArvStringView item_name;
    uint16_t count;
    bool has_data;
} ArvItemStackInfo;

typedef struct ArvItemsAPI {
    ArvResult (*stack_info)(void* ctx, ArvItemStackLocation location, ArvItemStackInfo* out_info);
    ArvResult (*load_stack_data)(void* ctx, ArvItemStackLocation location, ArvDataDocumentHandle* out_document);
    ArvResult (*load_stack_namespace_data)(void* ctx, ArvItemStackLocation location, ArvStringView namespace_id, ArvDataDocumentHandle* out_document);
    ArvResult (*save_stack_namespace_data)(void* ctx, ArvItemStackLocation location, ArvStringView namespace_id, ArvDataDocumentHandle document);
    ArvResult (*clear_stack_namespace_data)(void* ctx, ArvItemStackLocation location, ArvStringView namespace_id);
} ArvItemsAPI;
```

Rules:

* calls must occur on the sync simulation context
* `stack_info` returns `present = false` for valid empty stack locations
* load/save/clear calls require a present stack
* plugins may read full stack data but may only write or clear namespaces owned by their plugin pack
* stack data is bounded to 16 KiB per stack and uses the shared `DataDocument` model

---

## Entity API

The entity API exposes high-level entity operations without exposing the ECS registry.

Conceptual shape:

```cpp
struct EntityAPI {
    EntityID spawn(WorldID world, ResourceID entity_type, EntitySpawnParams params);
    bool destroy(EntityID entity);

    bool exists(EntityID entity);
    WorldID world(EntityID entity);

    Vec3 position(EntityID entity);
    bool set_position(EntityID entity, Vec3 position);

    bool add_tag(EntityID entity, ResourceID tag);
    bool remove_tag(EntityID entity, ResourceID tag);
    bool has_tag(EntityID entity, ResourceID tag);
};
```

Rules:

* plugins must not access `entt::registry` directly
* entity mutation must occur on the sync simulation context
* async systems should use snapshots or scheduled sync handoff
* plugin-owned entity metadata should use `PersistentDataAPI`, not custom raw components unless a future plugin component system is explicitly designed

---

## Event API

The event API lets plugins react to engine events.

Conceptual shape:

```cpp
struct EventAPI {
    EventSubscriptionID subscribe(
        ResourceID event_id,
        EventPriority priority,
        EventHandler handler
    );

    void unsubscribe(EventSubscriptionID subscription);
};
```

Example event IDs:

```text
arvane:event/player_join
arvane:event/player_leave
arvane:event/block_place
arvane:event/block_break
arvane:event/entity_spawn
arvane:event/entity_damage
arvane:event/chunk_load
arvane:event/server_start
arvane:event/server_stop
```

Rules:

* event IDs are resource IDs
* event payloads must be ABI-safe or wrapped by API functions
* event handlers run on the thread/context documented by the event
* handlers must not block
* cancelable events must document cancellation semantics
* event priority must be deterministic
* ties should be resolved by pack load order and then resource ID
* handlers must not store references to event payload memory after the callback returns

---

## Scheduler API

The scheduler API allows plugins to schedule sync and async jobs.

Two timing modes are available:

* millisecond-based schedules are the first-class default for real-time work such as countdowns, cooldown UI, announcements, and timers where wall-clock time matters
* tick-based schedules use explicit `_tick` names for simulation/gameplay work that should advance with the server tick

Tick delays remain relative tick counts, not absolute world tick indices. Millisecond delays are relative wall-clock durations measured with the engine's monotonic clock. Millisecond jobs are still dispatched from plugin scheduler pumps, so they are due after at least the requested duration has elapsed and run on the next scheduler pass.

SDK ABI shape:

```cpp
typedef void (*ArvJobCallback)(void* user_data);

typedef struct ArvSchedulerAPI {
    ArvResult (*sync_now)(
        void* ctx,
        ArvJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvResult (*sync_delayed)(
        void* ctx,
        ArvMilliseconds delay_milliseconds,
        ArvJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvResult (*sync_repeating)(
        void* ctx,
        ArvMilliseconds start_delay_milliseconds,
        ArvMilliseconds interval_milliseconds,
        ArvJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvResult (*async_now)(
        void* ctx,
        ArvJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvResult (*async_delayed)(
        void* ctx,
        ArvMilliseconds delay_milliseconds,
        ArvJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvResult (*async_repeating)(
        void* ctx,
        ArvMilliseconds start_delay_milliseconds,
        ArvMilliseconds interval_milliseconds,
        ArvJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvResult (*cancel)(void* ctx, ArvScheduledJobHandle handle);
    ArvResult (*sync_delayed_tick)(
        void* ctx,
        ArvTickIndex delay_ticks,
        ArvJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvResult (*sync_repeating_tick)(
        void* ctx,
        ArvTickIndex start_delay_ticks,
        ArvTickIndex interval_ticks,
        ArvJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvResult (*async_delayed_tick)(
        void* ctx,
        ArvTickIndex delay_ticks,
        ArvJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvResult (*async_repeating_tick)(
        void* ctx,
        ArvTickIndex start_delay_ticks,
        ArvTickIndex interval_ticks,
        ArvJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
} ArvSchedulerAPI;
```

`interval` is the preferred parameter name for the third repeating argument. It describes the repeat cadence after the first run more clearly than `restart_delay`.

The `_tick` functions are appended to the scheduler table for ABI compatibility. `ARV_API_VERSION` is unchanged.

The C++ wrapper follows the same naming: `sync(std::chrono::milliseconds, ...)`,
`async(std::chrono::milliseconds, ...)`, `sync_repeat(milliseconds, milliseconds, job)`,
and `async_repeat(milliseconds, milliseconds, job)` are real-time schedules. Tick-based
schedules are explicit: `sync_tick`, `async_tick`, `sync_repeat_tick`, and
`async_repeat_tick`.

### Sync Jobs

A sync job runs on the appropriate engine-owned sync context.

For a server plugin, this means the server simulation/tick context.

Sync jobs may:

* read live world state
* mutate world state through `WorldAPI`
* spawn or destroy entities through `EntityAPI`
* execute command-related logic
* interact with sync-only services

Sync jobs must not:

* block on long I/O
* perform expensive CPU work
* wait on async jobs in a way that stalls the simulation thread
* call rendering APIs directly

### Async Jobs

An async job runs on a worker thread.

Async jobs may:

* perform CPU-heavy pure computation
* parse plugin-owned config data
* prepare generated data
* query immutable snapshots
* use services explicitly marked thread-safe

Async jobs must not:

* mutate live world state
* mutate entities
* access raw engine internals
* call sync-only services
* assume job ordering across worker threads

Async jobs that need to affect gameplay should compute results asynchronously and then schedule a sync job to apply them.

Example:

```cpp
api->scheduler->async([](PluginJobContext* ctx) {
    GeneratedResult result = compute_expensive_result();

    ctx->scheduler->sync([result](PluginJobContext* sync_ctx) {
        sync_ctx->worlds->set_block(result.world, result.pos, result.state);
    });
});
```

### Repeating Jobs

Repeating jobs run until:

* explicitly canceled
* the owning plugin is disabled
* the owning world/server stops
* the job reports a fatal error

The scheduler should automatically cancel all outstanding jobs owned by a plugin during plugin shutdown.

---

## Command API

Commands are registry-backed. Native command definitions should be registered in
code, because the native implementation is the source of truth for static
metadata and behavior.

Do not create JSON or YAML files that merely duplicate native command metadata:

```yaml
name: heal
description: Restores health.
permission: example_plugin_pack.command.heal
```

The C ABI registers a command executor descriptor. The preferred C++ SDK wrapper
name is `register_command`; `register_executor` remains available as a
compatibility alias.

SDK ABI shape:

```cpp
typedef ArvResult (*ArvCommandCallback)(
    ArvCommandContext* command_ctx,
    void* user_data);

typedef struct ArvCommandDescriptor {
    ArvResourceID executor_id;
    ArvStringView command_name;
    ArvCommandDomain domain;
    ArvStringView permission;
    ArvStringView description;
    ArvStringView usage;
    bool public_to_players;
    bool console_only;
    ArvCommandCallback execute;
    void* user_data;
} ArvCommandDescriptor;

typedef struct ArvCommandAPI {
    ArvResult (*register_executor)(
        void* ctx,
        const ArvCommandDescriptor* descriptor);
} ArvCommandAPI;
```

Rules:

* command IDs are resource IDs
* native command descriptors own command name, domain, permission, description, usage, and callback
* data files should configure runtime policy or behavior, not duplicate static native metadata
* good uses for `data/commands.yaml` include aliases, command enable/disable configuration, permission overrides, command limits, macro commands, and scripted commands
* server-bound commands execute on the server and require permission checks
* client-bound commands execute locally and must not require server permission
* command output should use the Text API so colors, hover events, and click events remain structured

Example:

```cpp
plugin.commands().register_command(
    {
        .executor_id = "example_plugin_pack:heal",
        .name = "heal",
        .domain = arv::CommandDomain::ServerBound,
        .permission = "example_plugin_pack.command.heal",
        .description = "Restores health.",
        .usage = "/heal",
    },
    [](arv::CommandContext& ctx) {
        ctx.success("Healed.");
    }
);
```

Data may add policy or aliases:

```yaml
# data/commands.yaml
schema_version: 1

aliases:
  - alias: h
    command: heal
    visible_in_help: false
```

---

## Permission API

The permission API allows plugins to register and check permission nodes.

Conceptual shape:

```cpp
struct PermissionAPI {
    bool register_permission(PermissionDefinition definition);
    PermissionResult check(CommandSource source, ResourceID permission);
};
```

Rules:

* permission nodes should use a stable plugin namespace or public plugin domain
* command execution must check permissions on the authoritative side
* permissions should be stable across server restarts
* permission registration should happen during plugin registration
* permission checks must not depend on client claims

Examples:

```text
example_plugin_pack.command.heal
mason.command.set
example_plugin_pack.admin.reload
claims.region.bypass
```

First-party plugin pack IDs may keep an `arv_` prefix for code and pack identity
while exposing the shorter public permission domain. For example, the
`arv_mason` pack owns the `mason.*` permission nodes.

---

## Service API

The service API lets plugins expose APIs to other plugins through the engine.

This is useful for optional integrations without forcing every plugin to directly link against every other plugin.

Examples:

* economy plugins exposing account balances
* claims plugins exposing region ownership
* permissions plugins exposing group data
* quest plugins exposing quest state
* chat plugins exposing formatting hooks
* scoreboard plugins exposing line providers

### Service IDs

Services use resource IDs:

```text
economy:accounts
claims:regions
quests:quest_manager
chat:formatter
```

The service ID should describe the API contract, not just the provider implementation.

### SDK ABI Shape

```cpp
typedef struct ArvServiceDescriptor {
    ArvResourceID service_id;
    uint32_t api_version;
    int32_t priority;
    ArvServiceThreading threading;
    ArvStringView display_name;
    const void* api_table;
    void* user_data;
} ArvServiceDescriptor;

typedef struct ArvServiceRef {
    ArvServiceHandle handle;
    ArvResourceID service_id;
    uint32_t api_version;
    ArvStringView provider_pack_id;
    const void* api_table;
    void* user_data;
    bool valid;
} ArvServiceRef;

typedef struct ArvServiceAPI {
    ArvResult (*publish)(
        void* ctx,
        const ArvServiceDescriptor* descriptor,
        ArvServiceHandle* out_handle);
    ArvResult (*unpublish)(void* ctx, ArvServiceHandle handle);
    ArvResult (*get)(
        void* ctx,
        ArvResourceID service_id,
        uint32_t min_api_version,
        uint32_t max_api_version,
        ArvServiceRef* out_service);
    uint64_t (*list)(
        void* ctx,
        ArvResourceID service_id,
        uint32_t min_api_version,
        uint32_t max_api_version,
        ArvServiceRef* out_services,
        uint64_t max_services);
    void (*release)(void* ctx, ArvServiceRef service);
} ArvServiceAPI;
```

### Service API Tables

A service should expose an ABI-safe function table.

Example economy service:

```cpp
struct EconomyAccountsV1 {
    bool (*get_balance)(PlayerProfileID player, i64* out_balance);
    bool (*deposit)(PlayerProfileID player, i64 amount);
    bool (*withdraw)(PlayerProfileID player, i64 amount);
};
```

Provider:

```cpp
static EconomyAccountsV1 economy_api = {
    .get_balance = &get_balance,
    .deposit = &deposit,
    .withdraw = &withdraw,
};

ArvServiceHandle handle{};
plugin.services().publish(
    "economy:accounts",
    1,
    economy_api,
    &handle,
    "Example Economy",
    arv::ServiceThreading::SyncOnly
);
```

Consumer:

```cpp
auto ref = plugin.services().get("economy:accounts", 1, 1);

if (ref) {
    const auto* economy = ref.service.table<EconomyAccountsV1>();
    economy->deposit(player_id, 100);
}
```

### Service Rules

* services must be namespaced
* hard dependencies should still be declared as pack dependencies
* optional integrations should use service lookup
* services must declare threading rules
* service API versions must be explicit
* consumers must not assume a service exists
* consumers must not cache service pointers beyond the service lifetime
* provider priority must be deterministic
* if multiple providers exist, the highest-priority provider wins for `get`
* ties should be resolved by pack load order and then provider resource ID
* services are automatically unpublished when the owning plugin shuts down

### Service Threading Modes

Recommended threading modes:

```cpp
enum class ServiceThreading {
    SyncOnly,
    AsyncSafe,
    AnyThread
};
```

`SyncOnly` services may only be called from sync jobs or sync event handlers.

`AsyncSafe` services may be called from async jobs if the service contract says so.

`AnyThread` should be rare and only used for services designed for lock-free or internally synchronized access.

---

## Data, Config, And Persistent Storage APIs

Plugin config and plugin persistent data both use the shared `DataDocument` model. The C ABI exposes opaque `ArvDataDocumentHandle` values through `ArvDataAPI`; plugins create, mutate, load, save, and release document handles without seeing engine classes, YAML, JSON, filesystem paths, save internals, or STL types.

`DataDocument` supports null, bool, signed and unsigned integers, floating point values, strings, bytes, arrays, objects, and a top-level schema version. Returned string and byte views are borrowed from the owning document and remain valid only until that document is mutated or released.

Engine-side `DataDocument` also supports typed path decoding through `value<T>(path)` and `value_as<T>(path)`. Built-in codecs cover the primitive document types, and systems may add `DataCodec<T>` specializations for authored types such as chat message templates. This keeps config decoding reusable instead of hand-parsing strings at each call site.

Config remains a separate API because it is human-editable settings:

```cpp
plugin.config().global().load_file_or_default<MyConfig>("config.yaml");
plugin.config().global().save_file("config.yaml", config);
plugin.config().world(world).save_file("world_settings.yaml", config);
```

The C++ file helpers resolve the file name inside the calling plugin's config
directory, so `config.yaml` maps to
`arvane_home/config/plugins/<pack_id>/config.yaml`. Lower-level namespaced keys
such as `example_plugin:settings` remain available for advanced cases and are
still sanitized before touching storage.

Persistent data remains separate because it is save/runtime state with explicit lifetime scopes:

```cpp
plugin.persistent_data().server();
plugin.persistent_data().world(world);
plugin.persistent_data().player(player);
plugin.persistent_data().entity(entity);
plugin.persistent_data().block(world, block_pos);
plugin.persistent_data().chunk(world, chunk_pos);
```

Rules:

* config file helpers use the calling plugin pack ID automatically
* lower-level config and persistent keys must be namespaced resource IDs owned by the plugin pack
* keys are mapped to sanitized storage paths; plugins never provide raw filesystem paths
* config stores YAML by default and missing config returns `NotFound`
* persistent data stores backend-agnostic binary `DataDocument` records by default
* persistent records include schema versions and are preserved when the plugin is absent
* player scope resolves the SDK player handle to a stable server profile identity before touching storage
* entity scope uses persistent entity UUIDs; transient-only entities fail explicitly
* plugins should use C++ `Codec<T>` specializations with `load_or_default<T>()` and `save<T>()`

Recommended storage roots are implementation details, not plugin contracts:

```text
arvane_home/config/plugins/<pack_id>/<config_file>.yaml
arvane_home/saves/<world>/plugin_config/<world_id>/<pack_id>/<safe_key_path>.yaml
arvane_home/saves/<world>/plugin_data/<pack_id>/<scope>/<stable_identity>/<safe_key_path>.bin
```

This is the preferred way for plugins to store custom metadata without adding arbitrary ECS components or mutating engine save structures directly.

---

## Text API

The text API provides structured user-facing text.

It should support:

* plain text
* translatable text
* colors
* styles
* hover events
* click events

Conceptual shape:

```cpp
struct TextAPI {
    TextComponent literal(StringView text);
    TextComponent translatable(ResourceID translation_key);
    TextComponent color(TextComponent text, TextColor color);
    TextComponent hover(TextComponent text, HoverEvent event);
    TextComponent click(TextComponent text, ClickEvent event);
};
```

Rules:

* chat, command output, disconnect reasons, kick messages, and UI labels should use structured text
* plugins should not embed raw formatting control codes as the primary format
* translation keys should be namespaced
* server-authored text sent to clients must remain validated and bounded

### Chat Message Helper

Plugins that need stylized chat should prefer the SDK `ChatMessage` helper over manually building a network packet. A `ChatMessage` can be parsed from a string or loaded from a `DataDocument` config field, then rendered or sent with per-recipient variable resolution:

```cpp
auto message = arv::ChatMessage::from_config(plugin, config, "message");
message.value
    .with_channel("example_pack:alerts")
    .with_kind(arv::ChatMessageKind::Announcement)
    .broadcast();
```

Supported markup is intentionally small: `<#RRGGBB>`, named colors such as `<red>`, closing scopes with `</>`, `<hover:show_text:text>`, `<click:suggest_command|run_command|open_url|copy_to_clipboard:value>`, and `{variable_name}` placeholders. Malformed markup falls back to the raw string instead of partially interpreting unsafe input.
Starting a new color tag replaces the active color, so `<red>Mason<gray>></>`
needs only one close tag to reset following text.

### Variables API

Plugins may publish shared text variables through `plugin.variables()`. Variables have global and player scopes and are stored as strings after typed SDK setters convert bool, integer, floating point, or string values.

Variables also have a binding. `VariableBinding::Resolved` is the default and
means the server expands the variable before sending chat or scoreboard text.
`VariableBinding::Live` means the placeholder is sent to the client and the
client resolves it from replicated variable updates:

```cpp
plugin.variables().set_player(
    player,
    "example_pack:countdown",
    std::int64_t{1},
    arv::VariableBinding::Live);
```

Built-in unqualified names such as `player_name`, `player_health`, `player_max_health`, `online_player_count`, `tps`, and `world_name` are reserved. Plugin-authored variables should use namespaced IDs owned by the plugin pack, for example `example_pack:mana`.

The same variable resolver is used by chat message templates and scoreboard lines, so plugins should not maintain separate interpolation systems for each UI surface.

---

## Permissions API

The permissions API exposes Arvane’s authoritative permission system to plugins.

Permissions are the source of truth for gameplay authority. Game modes, groups, regions, temporary effects, and plugin rules all contribute to the final resolved permission state, but systems should query permissions instead of checking those sources directly.

Plugins use the permissions API to:

* check core engine permissions
* check custom node-based permissions
* register custom permission nodes
* contribute temporary permission modifiers
* contribute plugin/system permission overrides
* query resolved player permissions
* inspect and manage permission groups where authorized

The permissions API should not expose direct mutation of the resolved permission cache. Plugins add or remove permission sources; the engine recomputes and caches final resolved permissions.

---

### Permission Values

Permissions use a tri-state model:

```cpp
enum class PermissionValue {
    Unset,
    Allow,
    Deny,
};
```

Rules:

* `Allow` grants the permission when no higher-precedence denial exists
* `Deny` denies the permission
* `Unset` means the source has no opinion
* `Deny` takes precedence over `Allow`
* final permission results must never be ambiguous

---

### Core Permissions

Core permissions are fixed engine-level capabilities.

```cpp
enum class CorePermission {
    CanTakeDamage,
    CanModifyWorld,
    CanInteract,
    CanOpenInventory,
    HasCreativeInventory,
    CanFly,
    HasCollision,
    IsVisibleToOthers,
};
```

Core permissions must remain fast, fixed, and queryable without string lookup.

Systems should use core permissions for engine-critical checks:

```cpp
if (!permissions.can_modify_world(player)) {
    return;
}
```

They should not use game mode checks:

```cpp
if (player.mode == GameMode::Creative) {
    // Incorrect authority model.
}
```

---

### Custom Permissions

Custom permissions are node-based permissions intended for commands, plugins, server rules, and gameplay extensions.

Examples:

```text
arvane.command.teleport
arvane.command.gamemode
plugin.shop.buy
plugin.minigame.join
```

Custom permission nodes use dot-separated hierarchy.

Wildcards are supported:

```text
arvane.command.*
```

An exact match is more specific than a wildcard. A more specific wildcard is stronger than a broader wildcard.

Example:

```yaml
permissions:
  arvane.*: ALLOW
  arvane.command.*: DENY
  arvane.command.spawn: ALLOW
```

Resolution:

```text
arvane.chat.send       -> ALLOW
arvane.command.tp      -> DENY
arvane.command.spawn   -> ALLOW unless a higher or equal DENY applies
```

---

### Permission Sources

The permission pipeline uses layered sources.

From lowest to highest conceptual layer:

```text
5. Plugin/System Overrides
4. Region-Based Permissions
3. Temporary Modifiers
2. Player Overrides
   - Groups
   - Direct player overrides
1. Game Mode Base
```

Game mode provides the base layer.

Groups and direct player overrides provide persistent player-specific authority.

Temporary modifiers represent time-limited or runtime effects.

Regions and zones provide spatial context.

Plugin/system overrides provide server logic and custom mechanics.

The engine owns final resolution and caching.

---

### Conceptual Shape

```cpp
struct PermissionNodeDefinition {
    StringView node;
    StringView description;
    PermissionValue default_value;
};

struct CorePermissionSet {
    PermissionValue can_take_damage;
    PermissionValue can_modify_world;
    PermissionValue can_interact;
    PermissionValue can_open_inventory;
    PermissionValue has_creative_inventory;
    PermissionValue can_fly;
    PermissionValue has_collision;
    PermissionValue is_visible_to_others;
};

struct CustomPermissionEntry {
    StringView node;
    PermissionValue value;
};

struct PermissionOverrideSet {
    CorePermissionSet core;
    Span<CustomPermissionEntry> custom;
};

enum class PermissionOverrideLayer {
    TemporaryModifier,
    PluginSystemOverride,
};

struct PermissionOverrideDefinition {
    ResourceID id;
    PlayerID player;
    PermissionOverrideLayer layer;
    PermissionOverrideSet permissions;

    // Optional. If unset, the override remains until removed.
    Optional<TickIndex> expires_after_ticks;
};

struct ResolvedPermissions {
    bool can_take_damage;
    bool can_modify_world;
    bool can_interact;
    bool can_open_inventory;
    bool has_creative_inventory;
    bool can_fly;
    bool has_collision;
    bool is_visible_to_others;
};

struct PermissionsAPI {
    bool register_permission_node(const PermissionNodeDefinition& definition);

    bool check_core(PlayerID player, CorePermission permission);
    PermissionValue check_custom(PlayerID player, StringView node);

    ResolvedPermissions resolved_core(PlayerID player);

    PermissionOverrideHandle add_override(const PermissionOverrideDefinition& definition);
    bool remove_override(PermissionOverrideHandle handle);

    bool invalidate_player(PlayerID player);

    bool has_group(PlayerID player, ResourceID group_id);
    bool add_group(PlayerID player, ResourceID group_id);
    bool remove_group(PlayerID player, ResourceID group_id);

    Span<ResourceID> groups(PlayerID player);
};
```

The actual standalone Plugin SDK should expose this through ABI-safe function tables and opaque handles. This C++ shape is conceptual.

---

### Permission Groups

Permission groups are reusable permission containers.

Groups may define:

* core permission overrides
* custom permission nodes
* inheritance
* priority

Conceptual group definition:

```cpp
struct PermissionGroupDefinition {
    ResourceID id;
    i32 priority;

    Span<ResourceID> inherits;

    CorePermissionSet core;
    Span<CustomPermissionEntry> custom;
};
```

Plugins may register groups when they own the group namespace.

```cpp
struct PermissionsAPI {
    bool register_group(const PermissionGroupDefinition& definition);
};
```

Rules:

* group IDs must be stable
* plugin-defined group IDs must be namespaced
* group inheritance must not contain cycles
* missing inherited groups are validation errors
* priority defaults to `0`
* players may have multiple groups
* higher-priority groups override lower-priority groups
* `Deny` wins when conflicts remain

Groups are part of the Player Overrides layer.

---

### Plugin Overrides

Plugin/system overrides are for server logic and custom mechanics.

Examples:

* minigame plugin denies block breaking during a countdown
* combat arena plugin allows PvP inside a match
* tutorial plugin disables inventory access for one step
* admin tool temporarily grants flight

Plugins should add overrides through handles:

```cpp
PermissionOverrideHandle handle =
    permissions.add_override({
        .id = "example_minigame:match_rule",
        .player = player,
        .layer = PermissionOverrideLayer::PluginSystemOverride,
        .permissions = rule_permissions,
    });
```

The plugin must remove the override when it no longer applies:

```cpp
permissions.remove_override(handle);
```

Rules:

* overrides must be owned by the registering plugin
* overrides must not mutate the resolved cache directly
* overrides are automatically removed when the owning plugin shuts down
* temporary overrides may expire by tick
* adding or removing an override invalidates the affected player’s resolved permission cache

---

### Permission Events

The event API should expose permission-related events.

Recommended event IDs:

```text
arvane:event/permissions_changed
arvane:event/player_group_added
arvane:event/player_group_removed
arvane:event/permission_override_added
arvane:event/permission_override_removed
```

These events are useful for:

* updating UI
* refreshing cached plugin state
* updating scoreboard lines
* applying minigame rules
* auditing administrative actions

Permission events should be server-side authoritative.

---

### Rules

* plugins must query permissions, not game modes, for gameplay checks
* clients may use permissions for prediction or UI, but the server remains authoritative
* core permission checks must be fast
* custom permission checks may use cached wildcard resolution
* plugins must not store resolved permission snapshots indefinitely
* resolved permissions are invalidated on mode changes, group changes, region changes, temporary modifier updates, and plugin override changes
* permission node names should be stable
* plugin permission nodes should be prefixed with the plugin namespace or domain
* plugin shutdown removes plugin-owned overrides

---

## Regions API

The regions API exposes spatial gameplay rules to plugins.

Regions and zones are not just permissions. They are spatial context objects used for:

* spawn protection
* lobby areas
* minigame arenas
* protected builds
* PvP areas
* scripted gameplay spaces
* world-specific behavior overrides

Regions integrate with permissions as the Region-Based Permissions layer, but they remain a separate system.

Plugins use the regions API to:

* query regions at a position
* query a player’s active regions
* create runtime plugin-owned regions
* remove plugin-owned runtime regions
* register custom region flags
* read region flags
* respond to region enter/exit events

Persistent server/world regions should primarily be loaded from config or pack/server data. Runtime plugin-created regions are useful for minigames, temporary arenas, selections, scripted events, and admin tools.

---

### Region and Zone Model

A region is a named spatial volume with rules attached.

A zone is a shared rule bundle that regions may opt into.

Regions may define:

* shape
* world
* priority
* zones
* permission overrides
* custom flags
* enter/exit behavior

Zones may define:

* shared permission rules
* shared custom flags
* priority

Regions are scoped to a world or dimension.

A player can only be inside regions that belong to their current world.

---

### Region Shapes

The initial plugin API should expose AABB regions.

```cpp
struct RegionAabb {
    BlockPos min;
    BlockPos max;
};
```

Future shapes may include:

* cylinder
* sphere
* polygonal 2D footprint with height range

The first API should not overfit to future shape types. It should use a tagged shape structure so the ABI can grow cleanly.

```cpp
enum class RegionShapeKind {
    Aabb,
};

struct RegionShape {
    RegionShapeKind kind;

    union {
        RegionAabb aabb;
    };
};
```

---

### Region Flags

Region flags are named gameplay booleans or tri-state values.

Examples:

```text
arvane:pvp
arvane:fall_damage
example_minigame:in_match
example_claims:protected
```

Flags are separate from permissions.

A permission answers:

```text
Can this player do X?
```

A region flag answers:

```text
What rule applies in this space?
```

This distinction is important. Not every spatial rule is player-specific.

---

### Conceptual Shape

```cpp
enum class RegionRuleValue {
    Unset,
    Allow,
    Deny,
};

struct RegionFlagDefinition {
    ResourceID id;
    StringView description;
    RegionRuleValue default_value;
};

struct RegionPermissionRules {
    CorePermissionSet core;
    Span<CustomPermissionEntry> custom;
};

struct RegionFlagEntry {
    ResourceID flag;
    RegionRuleValue value;
};

struct RegionDefinition {
    ResourceID id;
    WorldID world;

    i32 priority;
    RegionShape shape;

    Span<ResourceID> zones;

    RegionPermissionRules permissions;
    Span<RegionFlagEntry> flags;

    bool persistent;
};

struct ZoneDefinition {
    ResourceID id;

    i32 priority;

    RegionPermissionRules permissions;
    Span<RegionFlagEntry> flags;
};

struct RegionQuery {
    WorldID world;
    Vec3 position;
};

struct RegionInfo {
    ResourceID id;
    WorldID world;
    i32 priority;
};

struct RegionsAPI {
    bool register_region_flag(const RegionFlagDefinition& definition);

    RegionHandle create_runtime_region(const RegionDefinition& definition);
    bool remove_runtime_region(RegionHandle handle);

    bool register_runtime_zone(const ZoneDefinition& definition);
    bool remove_runtime_zone(ResourceID zone_id);

    Span<RegionInfo> find_regions_at(const RegionQuery& query);
    Span<RegionInfo> active_regions(PlayerID player);

    bool is_player_in_region(PlayerID player, ResourceID region_id);

    RegionRuleValue region_flag(ResourceID region_id, ResourceID flag);
    RegionRuleValue query_flag(const RegionQuery& query, ResourceID flag);
};
```

The actual standalone Plugin SDK should expose this through ABI-safe result buffers, opaque handles, and explicit release calls.

---

### Runtime Regions

Runtime regions are plugin-owned regions created through the API.

Examples:

* temporary minigame arena bounds
* active event areas
* admin selection previews
* temporary no-build areas
* scripted dungeon rooms

Rules:

* runtime region IDs must be namespaced
* runtime regions are owned by the creating plugin
* runtime regions are removed when the owning plugin shuts down
* runtime regions may be persistent only if the engine exposes explicit persistence support
* persistent world-owned regions should usually be defined through world/server config
* creating or removing a region invalidates affected players’ resolved permission caches

---

### Region Priority

Regions have explicit priority.

Higher-priority regions override lower-priority regions within the region permission layer.

If priority is equal:

* `Deny` wins over `Allow`
* explicit values win over `Unset`
* deterministic ordering uses region ID as a final tie-breaker

Zones apply through regions.

Within the region layer:

```text
1. Zone rules
2. Region rules
3. Higher-priority region rules
```

---

### Region Events

The server tracks player region membership.

When membership changes, the event API should emit:

```text
arvane:event/player_enter_region
arvane:event/player_exit_region
arvane:event/player_regions_changed
```

These events are useful for:

* titles/messages
* minigame start triggers
* UI prompts
* temporary effects
* scoreboard updates
* audio/visual cues

Region events should include:

```cpp
struct PlayerRegionEvent {
    PlayerID player;
    WorldID world;
    ResourceID region_id;
};
```

Region event payloads should be exposed through the Event API using ABI-safe event contexts.

---

### Interaction With Permissions

Regions contribute to the Region-Based Permissions layer.

A region may deny block modification:

```yaml
core:
  can_modify_world: DENY
```

A plugin should still check:

```cpp
permissions.check_core(player, CorePermission::CanModifyWorld);
```

The plugin should not manually query regions and reconstruct permission results unless it is implementing diagnostics or an editor.

Regions do not change a player’s game mode by default.

A creative player entering spawn may remain creative while `can_modify_world` resolves to false because of region rules.

---

### Rules

* region logic is server authoritative
* clients must not decide region membership authoritatively
* region queries must be scoped to a world
* plugins must not manually scan internal region storage
* plugins must use `RegionsAPI` queries
* plugin-owned runtime regions must be removed on plugin shutdown
* region changes must invalidate affected permission caches
* high-volume region queries should use engine-owned spatial acceleration
* region APIs should not expose raw internal region containers

---

## Game Mode API

The game mode API exposes game mode presets to plugins.

Game modes are presets over permissions. They are not the source of truth.

Plugins use the game mode API to:

* query a player’s current game mode
* set a player’s game mode when authorized
* register custom game mode presets
* inspect game mode permission presets
* listen for game mode changes

Gameplay systems should not check game mode directly for authority. They should query the Permissions API.

---

### Built-In Game Modes

The built-in game modes are fixed identifiers and must not be renamed.

```text
survival
creative
spectator
lobby
noclip
```

These IDs may appear in:

* saves
* networking
* commands
* plugins
* server config

Built-in game modes are registered by base game behavior during runtime composition.

Generic systems should not depend on the base module. They should query resolved permissions.

---

### Custom Game Modes

Custom game modes are permission presets.

A custom game mode should define:

* stable ID
* optional base mode
* core permission overrides
* custom permission defaults
* display metadata

Example:

```yaml
game_modes:
  example_pack:builder_plus:
    base: creative
    overrides:
      is_visible_to_others: false
```

Custom plugin-defined game modes should use namespaced IDs to avoid collisions.

Built-in short IDs remain reserved.

---

### Conceptual Shape

```cpp
using GameModeID = StringView;

struct GameModeDefinition {
    GameModeID id;

    Optional<GameModeID> base;

    StringView display_name;
    StringView description;

    CorePermissionSet core;
    Span<CustomPermissionEntry> custom;
};

struct GameModeInfo {
    GameModeID id;
    StringView display_name;
    bool builtin;
};

struct GameModeChange {
    PlayerID player;
    GameModeID mode;
    ResourceID reason;
};

struct GameModeAPI {
    bool register_mode(const GameModeDefinition& definition);

    bool exists(GameModeID mode);
    GameModeInfo info(GameModeID mode);

    GameModeID current(PlayerID player);
    bool set(PlayerID player, GameModeID mode, ResourceID reason);

    CorePermissionSet base_core_permissions(GameModeID mode);

    Span<GameModeInfo> list_modes();
};
```

The actual standalone Plugin SDK should expose game mode IDs as ABI-safe string views and return lists through explicit output buffers or releaseable result handles.

---

### Setting Game Modes

Changing a player’s game mode should:

1. validate that the mode exists
2. verify that the caller is authorized
3. update the player’s stored game mode ID
4. recompute resolved permissions
5. notify the client if needed
6. emit a game mode change event

Recommended event ID:

```text
arvane:event/player_game_mode_changed
```

Changing game mode should not directly grant permanent permission groups or player overrides.

---

### Registering Game Modes

Custom game mode definitions should normally be loaded from pack data.

Native plugins may register mode definitions during the registration phase when useful, but data-driven definitions should be preferred.

Rules:

* built-in modes cannot be modified or renamed
* custom modes must use stable IDs
* plugin-defined custom modes should use namespaced IDs
* custom modes may inherit from a base mode
* inheritance cycles are invalid
* mode definitions must be locked after registry construction
* saves store the selected mode ID, not resolved permissions

---

### Interaction With Permissions

Game mode contributes the base permission layer.

If a player is in creative mode but a region denies world modification, the final resolved permission denies modification.

Plugins should use this:

```cpp
permissions.check_core(player, CorePermission::CanModifyWorld);
```

Not this:

```cpp
game_modes.current(player) == "creative";
```

GameModeAPI is for mode management and presentation, not authority checks.

---

### Rules

* game modes are server authoritative
* clients may use game mode for UI and prediction only
* all authoritative action checks must use resolved permissions
* changing mode invalidates resolved permission cache
* game mode IDs stored in saves must remain stable
* plugin-defined modes should not depend on native plugin code being present unless the owning pack requires it
* missing custom game mode on save load should produce a clear missing-content diagnostic

---

## Network API

The network API lets server-side plugins send existing `arv_protocol` packets to clients.

It is **not** a plugin-defined protocol extension system. Plugins do not register custom channels, define custom packet IDs, negotiate plugin channels, or send arbitrary packet payloads.

For ordinary rich chat, prefer the first-class `ChatMessage` helper. The lower-level network packet API remains available for explicitly exposed packet builders, but the chat helper handles config parsing, variable interpolation, per-recipient player variables, validation, channel selection, and send helpers.

The purpose of this API is narrow:

* allow plugins to send supported `ClientboundX` packets
* keep packet definitions centralized in `arv_protocol`
* preserve server-authoritative networking
* prevent plugins from bypassing protocol validation

---

### Protocol Ownership

All packets exposed through this API must be defined by `arv_protocol`.

Plugins may send packets such as:

```text
ClientboundChatMessage
ClientboundKickPlayer
ClientboundOpenScreen
ClientboundCloseScreen
ClientboundPlaySound
ClientboundTitle
ClientboundSetHealth
ClientboundSetGameMode
ClientboundUpdatePermissions
```

Only packets intentionally exposed to plugins should be available through the plugin API.

Plugins must not:

* define new packet IDs
* register packet channels
* send arbitrary byte payloads
* bypass packet validation
* send `ServerboundX` packets
* pretend to be a client
* mutate connection state directly

---

### Direction Rules

Packet naming follows Arvane protocol conventions:

```text
ServerboundX  -> client sends X to server
ClientboundX  -> server sends X to client
```

The plugin Network API is server-side and may only send `ClientboundX` packets.

Plugins do not send `ServerboundX` packets. Serverbound packets are client input and must be received through the normal networking pipeline, validated by the server, and handled by engine systems.

---

### Conceptual Shape

```cpp
struct NetworkAPI {
    bool send_to_player(PlayerID player, const ClientboundPacket& packet);

    bool send_to_players(Span<PlayerID> players, const ClientboundPacket& packet);

    bool broadcast(const ClientboundPacket& packet);

    bool broadcast_world(WorldID world, const ClientboundPacket& packet);
};
```

The standalone Plugin SDK should expose this through ABI-safe packet handles or packet builder functions rather than raw engine packet classes.

A C ABI shape may look like:

```cpp
typedef uint64_t ArvPacketHandle;

struct NetworkAPI {
    ArvPacketHandle create_clientbound_packet(ClientboundPacketKind kind);

    bool packet_set_field(
        ArvPacketHandle packet,
        PacketFieldID field,
        PacketFieldValue value
    );

    bool send_to_player(PlayerID player, ArvPacketHandle packet);
    bool send_to_players(const PlayerID* players, size_t count, ArvPacketHandle packet);
    bool broadcast(ArvPacketHandle packet);
    bool broadcast_world(WorldID world, ArvPacketHandle packet);

    void release_packet(ArvPacketHandle packet);
};
```

The exact implementation may use generated packet builders from `arv_protocol` if available.

---

### Typed Packet Builders

Prefer typed packet builder helpers over generic field mutation when practical.

Example conceptual C++ wrapper:

```cpp
network.send_to_player(
    player,
    ClientboundChatMessage {
        .message = text,
        .position = ChatPosition::System,
    }
);
```

For the ABI layer, this can be represented by explicit creation functions for exposed packet types:

```cpp
ArvPacketHandle create_clientbound_chat_message(TextHandle message);
ArvPacketHandle create_clientbound_kick_player(TextHandle reason);
ArvPacketHandle create_clientbound_title(TextHandle title, TextHandle subtitle);
```

This is safer than giving plugins arbitrary byte serialization access.

---

### Exposed Packet Set

The engine should maintain an explicit allowlist of protocol packets available to plugins.

Initial candidates:

```text
ClientboundChatMessage
ClientboundKickPlayer
ClientboundOpenScreen
ClientboundCloseScreen
ClientboundPlaySound
ClientboundTitle
ClientboundSetGameMode
ClientboundUpdatePermissions
```

Packets that affect core synchronization, registry state, chunk streaming, entity replication, login state, authentication, or transport internals should not be exposed casually.

Examples to avoid exposing directly:

```text
ClientboundChunkData
ClientboundRegistryData
ClientboundEntitySnapshot
ClientboundLoginAccepted
ClientboundKeepAlive
```

Those packets are owned by engine systems and should be sent only by the normal networking pipeline.

---

### Validation Rules

The network API must validate packets before sending.

Validation should check:

* packet kind is exposed to plugins
* target player is connected
* target player is in the correct protocol phase
* payload size is bounded
* text components are validated
* resource IDs are valid
* referenced screens, sounds, or assets are available to the client when required
* packet is legal for the target connection state

Invalid packets must fail cleanly and log diagnostics with the owning plugin ID.

---

### Authority Rules

Sending a packet does not grant authority.

Plugins must still use the correct engine APIs for authoritative state changes.

Examples:

* To change a player’s game mode, use `GameModeAPI`, not only `ClientboundSetGameMode`.
* To change permissions, use `PermissionsAPI`, not only `ClientboundUpdatePermissions`.
* To kick a player, use the server/player management API if one exists; `ClientboundKickPlayer` should be paired with connection closure by the engine.
* To modify world state, use `WorldAPI`; do not attempt to fake world changes with client packets.

Network packets are for client notification and presentation, not authoritative state ownership.

---

### Reliability

Packet reliability is determined by `arv_protocol` and the engine networking layer.

Plugins should not choose raw GameNetworkingSockets reliability flags directly.

The protocol definition should decide whether a packet is:

* reliable
* unreliable
* ordered
* unordered
* state-replicated
* event-like

The plugin API should expose packet sending at the protocol level, not the transport level.

---

### Client Compatibility

Plugins may only send packets that the connected client is expected to understand as part of the active `arv_protocol` version.

Because custom plugin packets are not supported here, no plugin channel negotiation is required.

If a packet depends on optional client-visible content from packs, the server must ensure that the client has compatible required packs before sending content references.

---

### Rules

* plugins may only send exposed `ClientboundX` packets
* plugins must not send `ServerboundX` packets
* plugins must not register custom channels
* plugins must not send arbitrary serialized payloads
* all packets must be defined in `arv_protocol`
* packet sending must respect connection state
* packet payloads must be validated and bounded
* transport reliability is owned by the protocol/network layer
* network packets must not bypass permissions, game mode logic, world state, or server authority
* packet send failures should return explicit errors and include plugin-scoped diagnostics

---

## World Generation API

World generation is primarily data-driven.

Plugins should usually extend world generation by adding data files under the pack data root:

```text
data/worldgen/
data/biomes.yaml
data/dimensions.yaml
```

Examples:

```text
data/biomes.yaml
data/worldgen/features/coral_patch.yaml
data/worldgen/generators/ocean_world.yaml
```

The engine’s worldgen data model owns:

* generator presets
* biome definitions
* biome selectors
* surface rules
* feature definitions
* ore definitions
* structure definitions

Native worldgen APIs should exist only for cases where pack data needs a new behavior type that the engine does not provide.

### Recommended Native Worldgen Extension Points

Conceptual shape:

```cpp
struct WorldGenAPI {
    bool register_noise_type(ResourceID id, const NoiseTypeVTable* noise_type);
    bool register_density_function_type(ResourceID id, const DensityFunctionVTable* density_type);
    bool register_biome_selector_type(ResourceID id, const BiomeSelectorVTable* selector_type);
    bool register_surface_condition_type(ResourceID id, const SurfaceConditionVTable* condition_type);
    bool register_feature_type(ResourceID id, const FeatureTypeVTable* feature_type);
    bool register_structure_type(ResourceID id, const StructureTypeVTable* structure_type);
    bool register_placement_constraint_type(ResourceID id, const PlacementConstraintVTable* constraint_type);
};
```

Example data:

```yaml
id: better_oceans:coral_patch
version: 1

type: better_oceans:coral_patch_feature

placement:
  region_size: 32
  attempts_per_region: 8
  chance: 0.5
  salt: "coral_patch"
```

Example native registration:

```cpp
api->worldgen->register_feature_type(
    "better_oceans:coral_patch_feature",
    &coral_patch_feature_type
);
```

### Worldgen Execution Rules

Native worldgen functions must be:

* deterministic
* stateless
* thread-safe
* versioned
* chunk-independent
* safe to run on worker threads

Worldgen functions must derive output only from:

* world seed
* generator version
* world-space coordinates
* validated worldgen definition data
* registry snapshots
* deterministic local RNG

Worldgen functions must not:

* access live world state
* mutate neighbor chunks
* use global RNG
* depend on execution order
* depend on wall-clock time
* depend on async job timing
* call sync-only services
* use mutable global state
* perform blocking I/O during generation

### No General Chunk Mutation Hook Initially

The first plugin API should not expose a broad `on_generate_chunk` hook that allows arbitrary chunk mutation.

A broad hook is difficult to validate for determinism, cross-chunk consistency, threading, and save compatibility.

Prefer narrow typed extension points:

* custom feature type
* custom structure type
* custom density function
* custom biome selector
* custom surface condition
* custom placement constraint

If a general hook is added later, it must still be stateless, deterministic, clipped to the current output chunk, and validated as part of the worldgen pipeline.

---

## Metrics API

The metrics API helps plugin authors understand performance impact.

Conceptual shape:

```cpp
struct MetricsAPI {
    TimerID begin_timer(ResourceID metric_id);
    void end_timer(TimerID timer);

    void counter_add(ResourceID metric_id, i64 amount);
    void gauge_set(ResourceID metric_id, f64 value);
};
```

Rules:

* metric IDs must be namespaced
* metrics should identify the owning plugin
* scheduler jobs should be attributable to plugins
* slow plugin event handlers and jobs should appear in diagnostics
* metrics should not require plugins to integrate with engine internals directly

Useful metrics:

```text
example_plugin_pack:jobs/pathfinding_ms
example_plugin_pack:events/block_break_ms
example_plugin_pack:storage/loaded_records
```

---

## Logger API

The logger API provides plugin-scoped diagnostics.

SDK ABI shape:

```cpp
typedef struct ArvLoggerAPI {
    void (*trace)(void* ctx, ArvStringView message);
    void (*debug)(void* ctx, ArvStringView message);
    void (*info)(void* ctx, ArvStringView message);
    void (*warn)(void* ctx, ArvStringView message);
    void (*error)(void* ctx, ArvStringView message);
} ArvLoggerAPI;
```

Rules:

* log output should identify the owning pack/plugin
* repeated log spam should be rate-limited by the engine or plugin
* errors during startup should include the lifecycle phase
* data validation errors should include resource ID and file path when available

---

## Useful Built-In Services

The engine should consider exposing these as first-class APIs or built-in services.

### Logging

Plugin-scoped logging with pack attribution.

### Configuration

Plugin-owned and world-owned YAML config.

### Persistent Data

Namespaced save data for worlds, entities, and block positions.

### Permissions

Permission node registration and checks for commands and gameplay actions.

### Text and Localization

Structured text, translation keys, colors, click events, and hover events.

### Metrics and Profiling

Per-plugin job, event, and service timing.

### Economy

Not necessarily an engine-default service, but the service API should make economy plugins straightforward.

Recommended service ID:

```text
economy:accounts
```

### Region Claims

Not necessarily an engine-default service, but useful for protection plugins and integrations.

Recommended service ID:

```text
claims:regions
```

### Chat Formatting

Useful for rank prefixes, chat channels, and formatting plugins.

Recommended service ID:

```text
chat:formatter
```

### Scoreboard Providers

Useful for plugins that contribute scoreboard lines without owning the entire scoreboard.

Recommended service ID:

```text
scoreboard:line_provider
```

### Quest State

Useful for RPG-style plugins.

Recommended service ID:

```text
quests:quest_manager
```

These should be implemented as services when they are optional ecosystem APIs rather than mandatory engine APIs.

---

## Threading Rules

Plugins must follow engine threading constraints.

| Operation                     | Allowed Context                                        |
| ----------------------------- | ------------------------------------------------------ |
| Register native handlers      | Registration phase only                                |
| Register services             | Registration phase or documented runtime service phase |
| Read live world state         | Sync context only                                      |
| Write live world state        | Sync context only                                      |
| Read immutable world snapshot | Sync or async                                          |
| Entity mutation               | Sync context only                                      |
| Heavy compute                 | Async job                                              |
| Blocking I/O                  | Avoid; if necessary, async only and bounded            |
| Rendering                     | Not exposed initially                                  |
| Registry mutation             | Not allowed after registry lock                        |
| Service calls                 | According to service threading declaration             |

Violations are plugin bugs. The engine should catch obvious violations in debug builds and emit diagnostics in release builds where practical.

---

## Data Ownership Rules

The engine owns core data.

Plugins receive:

* IDs
* handles
* immutable views
* copies
* explicit API references

Plugins must not:

* store raw engine pointers
* store references to event payload memory
* mutate memory not owned by the plugin
* assume handle layout
* assume runtime numeric IDs are stable across content sets
* bypass pack resource resolution

If a plugin needs long-lived data, it should store plugin-owned copies or use `PersistentDataAPI`.

---

## Native Plugin Safety

Native plugins are trusted code.

They can:

* crash the engine
* corrupt memory
* read and write files
* execute arbitrary process code
* block threads
* violate gameplay integrity if written incorrectly

Therefore:

* native plugin loading can be disabled with `native_plugins.enabled: false`
* native plugins require explicit allowlisting
* plugin load/unload must be logged
* ABI mismatches must fail cleanly
* plugin panics/errors must identify the owning pack
* multiplayer servers must not silently push native code to clients
* automatic pack download must not imply native code execution

The first implementation should not claim to sandbox native plugins.

---

## Capabilities

Plugin capabilities are optional policy metadata used to restrict or audit API access.

Example:

```yaml
native:
  api_version: 1
  entrypoint: "arvane_plugin_main"
  capabilities:
    - scheduler.sync
    - scheduler.async
    - world.read
    - world.write
    - entity.spawn
    - services.publish
    - commands.register
```

Capability enforcement can be introduced gradually.

Even before hard enforcement exists, declared capabilities are useful for diagnostics, server administration, and future sandboxing.

---

## Side Rules

Plugins may be:

```text
server
client
both
```

### Server Plugins

Server plugins may affect authoritative gameplay.

Examples:

* commands
* permissions
* economy
* claims
* gameplay systems
* worldgen behavior
* block and item behavior
* server-side services

Server plugins must not trust client input.

### Client Plugins

Client plugins are optional future work.

Examples:

* client-side UI integrations
* visual-only overlays
* local tools
* client-side service consumers

Client plugins must not alter authoritative gameplay state while connected to multiplayer.

### Both-Side Plugins

Both-side plugins require compatible server and client behavior.

Examples:

* custom UI protocol
* custom presentation behavior
* optional client-side visualization for server systems

Both-side native plugins require explicit trust on both sides. A server-required pack may require client assets and data, but it must not silently require native client code execution.

---

## Error Handling

Plugins must not throw exceptions across the ABI boundary.

API calls should return explicit results.

Conceptual shape:

```cpp
enum class ArvResultCode {
    Ok,
    InvalidArgument,
    InvalidState,
    PermissionDenied,
    NotFound,
    DuplicateID,
    UnsupportedVersion,
    ThreadViolation,
    InternalError
};

struct ArvResult {
    ArvResultCode code;
    const char* message;
};
```

Rules:

* startup failures disable the plugin
* runtime failures should be logged with pack attribution
* repeated failures may disable the plugin or specific handler
* errors should not corrupt registries or saves
* plugin diagnostics should include resource IDs when relevant

---

## Versioning

The plugin API is versioned independently from pack manifest schema versions and pack content versions.

The native manifest declares:

```yaml
native:
  api_version: 1
```

The plugin entrypoint receives an API table with the matching version.

Rules:

* the engine may support multiple plugin API versions
* incompatible versions must fail clearly
* API tables may grow by adding optional function pointers or versioned sub-tables
* behavior registrations should include explicit schema or API versions when needed
* service APIs must version their own contracts separately from the engine plugin API

---

## Minimal Example

Pack data:

```yaml
# data/blocks.yaml

blocks:
  - id: example_plugin_pack:glowing_block
    version: 1

    display_name: "Glowing Block"
    behavior: example_plugin_pack:glowing_block_behavior

    presentation:
      model: example_plugin_pack:model/block/glowing_block
```

Plugin code:

```cpp
#include <arv/plugin.hpp>

ARV_MAIN(plugin) {
    plugin.registries().register_block_behavior(
        "example_plugin_pack:glowing_block_behavior"
    );

    plugin.commands().register_command(
        {
            .executor_id = "example_plugin_pack:heal",
            .name = "heal",
            .domain = arv::CommandDomain::ServerBound,
            .permission = "example_plugin_pack.command.heal",
            .description = "Restores health.",
            .usage = "/heal",
        },
        [](arv::CommandContext& ctx) {
            ctx.success("Healed.");
        }
    );

    static arv::Job tick{[] {
        run_plugin_tick();
    }};
    plugin.scheduler().sync_repeat_tick(1, 20, tick);

    plugin.logger().info("Example Plugin Pack loaded.");

    return true;
}
```

Command data:

```yaml
# data/commands.yaml
schema_version: 1

aliases:
  - alias: h
    command: heal
    visible_in_help: false
```

---

## Explicit Non-Goals

The plugin API does not initially provide:

* direct chunk access
* direct ECS registry access
* arbitrary runtime block/item/entity registration
* arbitrary worldgen scripting
* native plugin sandboxing
* hot unloading during active gameplay
* client-side rendering hooks
* automatic plugin download and execution
* direct file access outside controlled plugin config/resource APIs
* guarantees that unsafe native code cannot crash the engine

These may be revisited later as separate, explicit designs.

---

## Future Extensions

Possible future extensions:

* client-side plugin API
* WASM or Lua scripting layer
* native plugin sandboxing
* development-mode hot reload
* plugin UI extension points
* custom client HUD hooks
* custom renderer extension points
* richer worldgen codecs
* plugin marketplace/distribution tooling
* automatic pack download with native-code safety gates

These extensions should not weaken the core model:

```text
Pack data owns content.
Native plugins provide behavior.
All access goes through explicit APIs.
```

---

## Summary

The plugin system extends Arvane with trusted native code while preserving the pack system as the core content model.

The stable model is:

```text
Pack
  assets/  -> presentation resources
  data/    -> authoritative definitions and configuration
  native/  -> optional privileged executable code
```

Native plugins should register behavior, services, command executors, event handlers, and worldgen extension types. Ordinary content should live in pack data and reference native behavior by resource ID.

This keeps Arvane modding deterministic, server-authoritative, data-driven, and compatible with save validation, multiplayer pack checks, and future tooling.
