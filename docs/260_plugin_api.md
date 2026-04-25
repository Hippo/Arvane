# Plugin Api

## Overview

The plugin system allows external modules to extend the engine using native C++.

Plugins can:

* register blocks, items, and content
* define gameplay systems
* react to engine events
* interact with worlds and entities
* register commands and UI hooks

Plugins **must not**:

* mutate engine internals directly
* bypass defined APIs
* assume memory layout of engine types

---

## Design Goals

* **Stable ABI boundary**
* **Explicit capabilities**
* **No hidden dependencies**
* **Deterministic lifecycle**
* **Server-authoritative compatibility**
* **Thread-safe by design**

---

## Plugin Model

Each plugin is a **dynamic library**:

* Windows: `.dll`
* Linux: `.so`

Loaded at runtime by the engine.

---

## Entry Point

Each plugin must export a single function:

```cpp
extern "C" ARV_PLUGIN_EXPORT
void arv_plugin_entry(arv::PluginHost* host);
```

* Called once during plugin load
* Used to register systems, content, and hooks

---

## PluginHost Interface

The engine provides a controlled interface:

```cpp
namespace arv {

struct PluginHost {
    RegistryAPI* registry;
    WorldAPI* world;
    EntityAPI* entities;
    EventAPI* events;
    SchedulerAPI* scheduler;
    CommandAPI* commands;
};
}
```

This is the **only entry point** into engine functionality.

---

## API Modules

---

### Registry API

Used for content registration.

```cpp
struct RegistryAPI {
    BlockID register_block(const BlockDefinition&);
    ItemID register_item(const ItemDefinition&);
};
```

Constraints:

* Must only be called during plugin initialization
* IDs are assigned at runtime
* No mutation after registration

---

### World API

Provides controlled world interaction.

```cpp
struct WorldAPI {
    BlockStateID get_block(WorldID, BlockPos);
    void set_block(WorldID, BlockPos, BlockStateID);
};
```

Rules:

* Writes must occur on simulation thread
* No direct chunk access
* May enqueue operations instead of immediate execution

---

### Entity API

```cpp
struct EntityAPI {
    EntityID spawn(WorldID, const EntityDefinition&);
    void destroy(EntityID);
};
```

Notes:

* Backed by `entt`
* Plugins do not access registry directly
* Only high-level operations exposed

---

### Event API

Plugins subscribe to engine events.

```cpp
struct EventAPI {
    template<typename Event>
    void subscribe(void(*handler)(const Event&));
};
```

Example events:

* `BlockPlacedEvent`
* `BlockBrokenEvent`
* `ChunkLoadedEvent`
* `PlayerJoinEvent`

---

### Scheduler API

Allows plugins to schedule work.

```cpp
struct SchedulerAPI {
    void enqueue_main_thread(Job);
    void enqueue_worker(Job);
};
```

Rules:

* World mutation → main/simulation thread
* Pure computation → worker threads
* No blocking inside jobs

---

### Command API

```cpp
struct CommandAPI {
    void register_command(const CommandDefinition&);
};
```

Example:

* `/give`
* `/tp`
* plugin-defined commands

---

## Lifecycle

### Load Order

1. Engine initializes
2. Core systems register
3. Plugins load
4. `arv_plugin_entry` called
5. Registries locked
6. Worlds start

---

### Shutdown

* Plugins receive shutdown event
* Must release resources
* No further engine calls allowed

---

## Threading Rules

Plugins **must follow engine threading model**:

| Operation        | Allowed Thread      |
| ---------------- | ------------------- |
| Register content | Init only           |
| Read world       | Snapshot / safe API |
| Write world      | Simulation thread   |
| Heavy compute    | Worker threads      |
| Rendering        | Not exposed         |

Violations = undefined behavior

---

## Data Ownership Rules

* Engine owns all core data
* Plugins receive:

  * copies
  * handles
  * IDs

Plugins must **never store raw pointers** to engine memory.

---

## Events

### Example Event

```cpp
struct BlockPlacedEvent {
    WorldID world;
    BlockPos pos;
    BlockStateID state;
};
```

Usage:

```cpp
host->events->subscribe<BlockPlacedEvent>([](const BlockPlacedEvent& e) {
    // react
});
```

---

## Plugin Systems

Plugins can define systems via scheduler:

```cpp
host->scheduler->enqueue_main_thread([] {
    // per-tick logic
});
```

Future extension:

* fixed-tick hooks
* system phases

---

## Capabilities (Future Extension)

Plugins may declare capabilities:

```yaml
capabilities:
  - world.write
  - entity.spawn
```

Engine may restrict plugins in sandboxed environments.

---

## Networking Considerations

* Plugins run **server-side**
* Client plugins are optional and separate
* All gameplay authority is server-controlled

Plugins must:

* not assume client execution
* not trust client input

---

## Versioning

Each plugin declares:

```cpp
#define ARV_PLUGIN_API_VERSION 1
```

Engine validates compatibility at load time.

---

## Error Handling

Plugins must:

* not throw across API boundary
* handle failures gracefully

Engine may:

* disable plugin on error
* log diagnostics

---

## Minimal Example

```cpp
extern "C" ARV_PLUGIN_EXPORT
void arv_plugin_entry(arv::PluginHost* host) {

    host->registry->register_block({
        .name = "example:glowing_block"
    });

    host->events->subscribe<BlockPlacedEvent>([](const BlockPlacedEvent& e) {
        // do something
    });
}
```

---

## Explicit Non-Goals

* No direct chunk access
* No direct ECS registry access
* No scripting layer (for now)
* No client-side rendering hooks (yet)

---

## Future Extensions

* Client-side plugin API
* UI integration hooks
* scripting (WASM/Lua)
* hot reload
* sandboxing/permissions

---

## Summary

The plugin system is:

* **strictly controlled**
* **data-driven where possible**
* **thread-safe by design**
* **server-authoritative**

This ensures:

* stable modding ecosystem
* clean engine boundaries
* compatibility with AI-generated plugins

---

