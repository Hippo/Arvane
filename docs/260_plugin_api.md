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
```

The presence of `native` metadata does not automatically load the plugin.

Native plugin loading requires explicit runtime policy:

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
* client-side native plugins, if supported later, require separate explicit trust

---

## Content Authoring Model

### Preferred Model

Plugin packs should define content through pack data:

```text
data/example_plugin_pack/blocks/glowing_block.yaml
data/example_plugin_pack/items/glowing_wand.yaml
data/example_plugin_pack/entities/crystal_golem.yaml
data/example_plugin_pack/worldgen/features/crystal_cluster.yaml
data/example_plugin_pack/commands/heal.yaml
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

C++ convenience wrappers may exist in a plugin SDK, but the actual binary interface should avoid:

* C++ standard library types
* exceptions crossing the ABI boundary
* RTTI assumptions
* engine-owned object layouts
* raw ownership transfer without explicit destroy functions

Conceptual entrypoint:

```cpp
extern "C" ARV_PLUGIN_EXPORT
bool arvane_plugin_main(const ArvPluginAPI* api);
```

The plugin returns `true` if initialization succeeds.

The plugin returns `false` if it cannot initialize. The engine should then disable that plugin and report diagnostics.

Implemented ABI surface:

* The C ABI lives in the standalone Plugin SDK at `sdk/arvane_plugin/include/arvane/plugin/api.h`.
* Plugins include SDK headers and link only the `Arvane::PluginSDK` CMake `INTERFACE` target.
* The engine implements the ABI in `engine/arv_plugin/` and passes function tables to plugins at load time.
* API version `1` is exposed as `ARV_PLUGIN_API_VERSION`.
* `pack.yaml` native metadata must declare `api_version: 1`, a C-symbol entrypoint, and a library for the current platform key.
* Native libraries are loaded only when `native_plugins.enabled` is true and the owning pack is listed in `native_plugins.allowed_packs`.
* Implemented API tables include context, logger, behavior registration, command executor registration, event subscription, sync/async scheduling, service publish/lookup, data resource read/debug path, plugin config byte load/save, basic world/entity access, and metrics counters.
* Persistent data, network, permissions, rich text helpers, and native worldgen type registries return `Unsupported` unless the corresponding engine extension points are present.

Plugin author build surface:

```cmake
find_package(ArvanePluginSDK REQUIRED)

add_library(example_plugin SHARED
    src/example_plugin.cpp
)

target_link_libraries(example_plugin
    PRIVATE
        Arvane::PluginSDK
)
```

`Arvane::PluginSDK` provides include paths, compile features, and optional
header-only C++ wrappers. It must not pull in engine link libraries.

---

## Plugin API Table

The engine exposes functionality through versioned SDK API tables.

Conceptual shape:

```cpp
typedef struct ArvPluginAPI {
    uint32_t api_version;

    void* context_ctx;
    const ArvPluginContextAPI* context;

    void* registries_ctx;
    const ArvPluginRegistriesAPI* registries;

    void* scheduler_ctx;
    const ArvPluginSchedulerAPI* scheduler;

    void* commands_ctx;
    const ArvPluginCommandsAPI* commands;

    void* services_ctx;
    const ArvPluginServicesAPI* services;

    void* events_ctx;
    const ArvPluginEventsAPI* events;

    void* logger_ctx;
    const ArvPluginLoggerAPI* logger;
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
typedef struct ArvPluginContextAPI {
    ArvStringView (*pack_id)(void* ctx);
    ArvStringView (*pack_version)(void* ctx);
    ArvStringView (*side)(void* ctx);
    uint32_t (*namespace_count)(void* ctx);
    ArvStringView (*namespace_at)(void* ctx, uint32_t index);
    bool (*owns_namespace)(void* ctx, ArvStringView name_space);
} ArvPluginContextAPI;
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
    ArvPluginResult (*register_block_behavior)(void* ctx, ArvResourceID id);
    ArvPluginResult (*register_item_behavior)(void* ctx, ArvResourceID id);
    ArvPluginResult (*register_entity_goal)(void* ctx, ArvResourceID id);
    ArvPluginResult (*register_entity_behavior)(void* ctx, ArvResourceID id);
    ArvPluginResult (*register_component_codec)(void* ctx, ArvResourceID id);
    ArvPluginResult (*register_loot_function)(void* ctx, ArvResourceID id);
    ArvPluginResult (*register_recipe_type)(void* ctx, ArvResourceID id);
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
struct WorldAPI {
    BlockStateID get_block(WorldID world, BlockPos pos);
    bool set_block(WorldID world, BlockPos pos, BlockStateID state);

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

SDK ABI shape:

```cpp
typedef void (*ArvPluginJobCallback)(void* user_data);

typedef struct ArvSchedulerAPI {
    ArvPluginResult (*sync_now)(
        void* ctx,
        ArvPluginJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvPluginResult (*sync_delayed)(
        void* ctx,
        ArvTickIndex delay_ticks,
        ArvPluginJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvPluginResult (*sync_repeating)(
        void* ctx,
        ArvTickIndex start_delay_ticks,
        ArvTickIndex interval_ticks,
        ArvPluginJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvPluginResult (*async_now)(
        void* ctx,
        ArvPluginJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvPluginResult (*async_delayed)(
        void* ctx,
        ArvTickIndex delay_ticks,
        ArvPluginJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvPluginResult (*async_repeating)(
        void* ctx,
        ArvTickIndex start_delay_ticks,
        ArvTickIndex interval_ticks,
        ArvPluginJobCallback callback,
        void* user_data,
        ArvScheduledJobHandle* out_handle);
    ArvPluginResult (*cancel)(void* ctx, ArvScheduledJobHandle handle);
} ArvSchedulerAPI;
```

`interval` is the preferred parameter name for the third `sync_repeat` / `async_repeat` argument. It describes the repeat cadence after the first run more clearly than `restart_delay`.

The `TickIndex` delay parameters are relative tick counts. They are not absolute world tick indices.

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

Commands are registry-backed and should integrate with pack data.

Command definitions may live in pack data:

```text
data/<namespace>/commands/
```

Native plugins register command executors for command IDs.

SDK ABI shape:

```cpp
typedef ArvPluginResult (*ArvPluginCommandCallback)(
    ArvPluginCommandContext* command_ctx,
    void* user_data);

typedef struct ArvPluginCommandDescriptor {
    ArvResourceID executor_id;
    ArvStringView command_name;
    ArvPluginCommandDomain domain;
    ArvStringView permission;
    ArvStringView description;
    ArvStringView usage;
    bool public_to_players;
    bool console_only;
    ArvPluginCommandCallback execute;
    void* user_data;
} ArvPluginCommandDescriptor;

typedef struct ArvCommandAPI {
    ArvPluginResult (*register_executor)(
        void* ctx,
        const ArvPluginCommandDescriptor* descriptor);
} ArvCommandAPI;
```

Rules:

* command IDs are resource IDs
* command syntax and metadata should be data-driven where possible
* native code supplies execution behavior
* server-bound commands execute on the server and require permission checks
* client-bound commands execute locally and must not require server permission
* command output should use the Text API so colors, hover events, and click events remain structured

Example:

```yaml
id: example_plugin_pack:heal
literal: "heal"
domain: server_bound
permission: example_plugin_pack.command.heal
executor: example_plugin_pack:heal_executor
```

```cpp
ArvPluginCommandDescriptor descriptor{};
descriptor.executor_id = arv_cstr("example_plugin_pack:heal");
descriptor.command_name = arv_cstr("heal");
descriptor.domain = ARV_PLUGIN_COMMAND_SERVER_BOUND;
descriptor.execute = &heal_executor;

api->commands->register_executor(
    api->commands_ctx,
    &descriptor
);
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

* permission IDs should be namespaced
* command execution must check permissions on the authoritative side
* permissions should be stable across server restarts
* permission registration should happen during plugin registration
* permission checks must not depend on client claims

Examples:

```text
example_plugin_pack.command.heal
example_plugin_pack.admin.reload
claims.region.bypass
```

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
typedef struct ArvPluginServiceDescriptor {
    ArvResourceID service_id;
    uint32_t api_version;
    int32_t priority;
    ArvPluginServiceThreading threading;
    ArvStringView display_name;
    const void* api_table;
    void* user_data;
} ArvPluginServiceDescriptor;

typedef struct ArvPluginServiceRef {
    ArvServiceHandle handle;
    ArvResourceID service_id;
    uint32_t api_version;
    ArvStringView provider_pack_id;
    const void* api_table;
    void* user_data;
    bool valid;
} ArvPluginServiceRef;

typedef struct ArvServiceAPI {
    ArvPluginResult (*publish)(
        void* ctx,
        const ArvPluginServiceDescriptor* descriptor,
        ArvServiceHandle* out_handle);
    ArvPluginResult (*unpublish)(void* ctx, ArvServiceHandle handle);
    ArvPluginResult (*get)(
        void* ctx,
        ArvResourceID service_id,
        uint32_t min_api_version,
        uint32_t max_api_version,
        ArvPluginServiceRef* out_service);
    uint64_t (*list)(
        void* ctx,
        ArvResourceID service_id,
        uint32_t min_api_version,
        uint32_t max_api_version,
        ArvPluginServiceRef* out_services,
        uint64_t max_services);
    void (*release)(void* ctx, ArvPluginServiceRef service);
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

ArvPluginServiceDescriptor descriptor{};
descriptor.service_id = arv_cstr("economy:accounts");
descriptor.api_version = 1;
descriptor.priority = 0;
descriptor.threading = ARV_PLUGIN_SERVICE_SYNC_ONLY;
descriptor.display_name = arv_cstr("Example Economy");
descriptor.api_table = &economy_api;

ArvServiceHandle handle{};
api->services->publish(api->services_ctx, &descriptor, &handle);
```

Consumer:

```cpp
ArvPluginServiceRef ref{};
api->services->get(
    api->services_ctx,
    arv_cstr("economy:accounts"),
    1,
    1,
    &ref
);

if (ref.valid) {
    auto* economy = static_cast<const EconomyAccountsV1*>(ref.api_table);
    economy->deposit(player_id, 100);
    api->services->release(api->services_ctx, ref);
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

## Config API

The config API gives plugins access to plugin-owned configuration.

Conceptual shape:

```cpp
struct ConfigAPI {
    ConfigDocument load_plugin_config(ResourceID config_id);
    bool save_plugin_config(ResourceID config_id, ConfigDocument document);
    ConfigDocument load_world_config(WorldID world, ResourceID config_id);
    bool save_world_config(WorldID world, ResourceID config_id, ConfigDocument document);
};
```

Recommended locations:

```text
arvane_home/config/plugins/<pack_id>.yaml
arvane_home/saves/<world>/plugin_config/<pack_id>.yaml
```

Rules:

* pack data is content, not mutable runtime config
* plugin config should not modify registry construction after startup unless a controlled reload flow exists
* config files should be versioned
* config diagnostics must identify the plugin and config file
* plugins should avoid reading arbitrary filesystem paths directly

---

## Persistent Data API

The persistent data API lets plugins store plugin-owned save data.

Conceptual shape:

```cpp
struct PersistentDataAPI {
    bool set_world_data(WorldID world, ResourceID key, Bytes data);
    bool get_world_data(WorldID world, ResourceID key, Bytes* out_data);

    bool set_entity_data(EntityID entity, ResourceID key, Bytes data);
    bool get_entity_data(EntityID entity, ResourceID key, Bytes* out_data);

    bool set_block_data(WorldID world, BlockPos pos, ResourceID key, Bytes data);
    bool get_block_data(WorldID world, BlockPos pos, ResourceID key, Bytes* out_data);
};
```

Rules:

* keys must be namespaced
* stored data must include a plugin-defined version
* missing plugin data should not corrupt the rest of the save
* plugin-owned data should be recoverable or explicitly removable if the plugin is missing
* large persistent data should be chunked or indexed instead of attached to frequently loaded objects without limits

This API is the preferred way for plugins to store custom metadata without adding arbitrary ECS components or mutating engine save structures directly.

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

---

## Network API

The network API is for plugin-defined protocol extensions.

This should be optional and conservative because Arvane’s core gameplay protocol must remain stable and server-authoritative.

Conceptual shape:

```cpp
struct NetworkAPI {
    bool register_channel(ResourceID channel_id, ChannelDescriptor descriptor);

    bool send_to_client(PlayerID player, ResourceID channel_id, Bytes payload);
    bool send_to_server(ResourceID channel_id, Bytes payload);
};
```

Rules:

* channel IDs must be namespaced
* plugin network channels must be negotiated during connection setup
* server-side plugins must not assume the client has a matching native plugin
* missing client plugin support should fail gracefully or disable optional features
* all server-received payloads must be treated as untrusted
* payload sizes must be bounded
* plugin channels must not bypass core permission and authority checks

Packet naming inside plugin protocol documentation should follow Arvane naming conventions:

```text
ServerboundX  -> client sends X to server
ClientboundX  -> server sends X to client
```

Examples:

```text
ServerboundQuestAction
ClientboundQuestToast
ServerboundCustomUIScreenAction
ClientboundCustomUIScreenOpen
```

---

## World Generation API

World generation is primarily data-driven.

Plugins should usually extend world generation by adding data files under their pack namespace:

```text
data/<namespace>/worldgen/
data/<namespace>/biomes/
data/<namespace>/dimensions/
```

Examples:

```text
data/better_oceans/biomes/kelp_forest.yaml
data/better_oceans/worldgen/features/coral_patch.yaml
data/better_oceans/worldgen/generators/ocean_world.yaml
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

* native plugins are disabled by default
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
# data/example_plugin_pack/blocks/glowing_block.yaml

id: example_plugin_pack:glowing_block
version: 1

display_name: "Glowing Block"
behavior: example_plugin_pack:glowing_block_behavior

presentation:
  model: example_plugin_pack:model/block/glowing_block
```

Plugin code:

```cpp
#include <arvane/plugin/api.h>

extern "C" ARV_PLUGIN_EXPORT
bool arvane_plugin_main(const ArvPluginAPI* api) {
    if (!api || api->api_version != ARV_PLUGIN_API_VERSION) {
        return false;
    }

    api->registries->register_block_behavior(
        api->registries_ctx,
        arv_cstr("example_plugin_pack:glowing_block_behavior")
    );

    ArvPluginCommandDescriptor command{};
    command.executor_id = arv_cstr("example_plugin_pack:heal");
    command.command_name = arv_cstr("heal");
    command.domain = ARV_PLUGIN_COMMAND_SERVER_BOUND;
    command.execute = &heal_command_executor;
    api->commands->register_executor(
        api->commands_ctx,
        &command
    );

    ArvScheduledJobHandle job{};
    api->scheduler->sync_repeating(
        api->scheduler_ctx,
        1,
        20,
        &run_plugin_tick,
        nullptr,
        &job
    );

    api->logger->info(
        api->logger_ctx,
        arv_cstr("Example Plugin Pack loaded.")
    );

    return true;
}
```

Command data:

```yaml
# data/example_plugin_pack/commands/heal.yaml

id: example_plugin_pack:heal
version: 1

literal: "heal"
domain: server_bound
permission: example_plugin_pack.command.heal
executor: example_plugin_pack:heal
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
