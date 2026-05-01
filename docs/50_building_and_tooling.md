# Building and Tooling

## Purpose

This document defines the core build system, development tooling, and third-party libraries used in Arvane.

It exists to ensure consistency across the codebase and to provide a clear reference for contributors regarding the technologies that are considered standard within the project.

---

## Language

Arvane is written in **C++20**.

The language choice prioritizes:

* performance and control over memory/layout
* compatibility with native libraries and tooling
* suitability for engine-level systems (rendering, networking, simulation)

All code should follow modern C++ practices where they improve clarity, safety, or maintainability.

---

## Build System

Arvane uses **CMake** as its build system.

CMake is the single source of truth for:

* project configuration
* dependency integration
* build targets
* cross-platform support

All development environments and IDE integrations should rely on CMake rather than custom project files.

---

## Rendering

Arvane targets **OpenGL 3.3 Core Profile**.

OpenGL is used for:

* all rendering operations
* GPU resource management
* shader-based rendering pipelines

### OpenGL Loader

Arvane uses **GLAD** for OpenGL function loading.

GLAD is responsible for:

* loading OpenGL function pointers at runtime
* ensuring compatibility across platforms and drivers

---

## Windowing and Input

Arvane uses **GLFW** for:

* window creation
* OpenGL context management
* input handling (keyboard, mouse, controllers)
* monitor and display queries

GLFW usage should remain confined to platform/application layers.

---

## Entity Component System

Arvane uses **EnTT** as its ECS library.

EnTT is used for:

* entity and component storage
* efficient iteration over component data
* event and signal utilities where appropriate

EnTT is a foundational dependency for world-level simulation systems.

---

## Networking

Arvane uses **GameNetworkingSockets** for its networking layer.

GameNetworkingSockets provides:

* UDP-based communication
* reliable and unreliable messaging
* packet fragmentation and reassembly
* connection management

The networking library is responsible for transport-level concerns only.
All protocol design, replication logic, and message handling are implemented within the engine.

---

## Threading and Task Execution

Arvane uses a combination of:

* **Taskflow** for task scheduling and parallel execution
* **C++ standard library threading primitives** for low-level concurrency

### Taskflow

Taskflow is used for:

* task graph construction
* parallel job execution
* dependency management between tasks
* worker thread pool management

It serves as the foundation for engine-level scheduling and asynchronous work.

---

### Standard Library Concurrency

The following C++ standard library facilities are used:

* `std::jthread`
* `std::mutex`
* `std::shared_mutex`
* `std::atomic`
* `std::condition_variable`
* `std::stop_token`

These are used for:

* thread lifecycle management
* synchronization
* coordination between systems
* safe shared state handling where necessary

---

## Asset and Data Libraries

### Image Loading

Arvane uses **stb_image** for:

* loading image files (PNG, JPEG, etc.)
* decoding texture data for GPU upload

---

### Font Rendering and Text Shaping

Arvane uses:

* **FreeType** for font rasterization
* **HarfBuzz** for text shaping

These are used for:

* glyph rasterization
* proper text layout and shaping
* Unicode-aware text rendering

---

### Texture Atlas Packing

Arvane uses **rectpack2D** for:

* rectangle packing during atlas generation
* efficient texture atlas construction

---

### Data Serialization

Arvane uses:

* **nlohmann/json** for JSON parsing and serialization
* **yaml-cpp** for YAML parsing and serialization

These are used for:

* configuration files
* data-driven definitions (blocks, world gen, etc.)
* tooling and asset pipelines

---

## Build Configurations

Arvane supports the following build configurations:

### Debug

* debug symbols enabled
* minimal optimization
* assertions and validation enabled

### Release

* full optimization
* debug-only checks removed or reduced
* minimal logging

### RelWithDebInfo

* optimized build with debug symbols
* suitable for profiling and performance debugging

### Profiling Options

`ARVANE_ENABLE_PROFILING` controls all built-in profiling instrumentation, including frame sampling, timing histograms, queue-latency recording, and Tracy zone macros. Set it to `OFF` for FPS comparisons where profiling overhead must be removed from the hot path.

`ARVANE_ENABLE_TRACY` controls Tracy integration only when `ARVANE_ENABLE_PROFILING` is also enabled. A profiling build can keep internal timing counters without linking Tracy by setting `ARVANE_ENABLE_PROFILING=ON` and `ARVANE_ENABLE_TRACY=OFF`.

Example comparison builds:

```sh
cmake -S . -B build-profiled -DARVANE_ENABLE_PROFILING=ON -DARVANE_ENABLE_TRACY=ON
cmake -S . -B build-noprofile -DARVANE_ENABLE_PROFILING=OFF
```

### Debug Profiler Expectations

The in-game Profiling tab is intended for spike diagnosis, not only average timing. Aggregate timing tables report p50, p95, and p99 over the current rolling sample window:

* p50 is the median sampled duration.
* p95 is the sample at or above 95% of recorded durations.
* p99 is the sample at or above 99% of recorded durations.

Aggregate timing answers whether a subsystem is usually expensive. Worst-event tracking answers which individual recent sample spiked, on which frame, on which thread/timeline, and with what bounded context string such as a chunk coordinate, job type, queue name, or render pass.

Profiler instrumentation rules:

* No heap allocation per timing sample in hot paths.
* No unbounded history. Use fixed-size rings, bounded Top-N views, or rolling windows.
* Avoid locks on worker/job hot paths; prefer atomics, per-thread samples, and end-of-frame aggregation.
* Context strings must be bounded and copied into fixed storage, or use stable labels.
* Subsystem counters should be updated at ownership boundaries such as job submit/complete, chunk lifecycle changes, mesh enqueue/upload, and snapshot copy-on-write.
* Do not scan all chunks every frame just to draw profiling UI. If a value matters every frame, maintain a counter where the state changes.

Chunk workload counters should distinguish loaded chunks, active/simulated chunks, visible/renderable chunks, requested/generating chunks, lighting backlog, meshing backlog, pending mesh uploads, and per-frame snapshot creation/copy cost.

Queue pressure counters should report current queued jobs, rolling peak queued jobs, submitted/completed jobs for the frame, active workers, and idle workers or idle percentage. These counters are separate from queue latency timing: latency shows how long a job waited before starting, while pressure shows whether the queue is keeping up.

---

## Assertions and Logging

### Assertions

Assertions are used to enforce:

* internal invariants
* correct API usage
* valid engine state

Assertions are primarily active in debug builds.

---

### Logging

Logging is organized by subsystem and used for:

* diagnostics
* error reporting
* debugging complex behavior

Logging should remain intentional and avoid unnecessary noise.

---

## Testing

Arvane supports automated testing for:

* core utilities
* math systems
* data structures
* serialization and networking logic
* engine subsystems where deterministic validation is practical

Tests should be fast, focused, and reliable.

---

## Formatting and Code Quality

### Formatting

A consistent code formatting standard is enforced across the project.

Formatting is considered part of the codebase contract and should not vary between contributors.

---

### Static Analysis

Static analysis tools are used to detect:

* lifetime and ownership issues
* threading mistakes
* incorrect API usage
* dead or suspicious code paths

---

## Configuration Search and Default Policy

Arvane should use a simple, predictable configuration discovery model during development.

The engine must not rely on hidden global state, implicit user directories, or platform-specific install paths during early milestones.

The default project layout assumes:

```text
arvane/
    arvane_home/
        assets/
        plugins/
        config/
        saves/
        logs/
        cache/
    docs/
    engine/
    apps/
    CMakeLists.txt
```

`arvane_home/` is the canonical development runtime home directory.

Examples of runtime files include:

* entity definitions
* textures
* shaders
* fonts
* UI definitions
* test assets
* config-driven block, item, and world generation definitions

The engine may later support mounted resource packs or user-provided asset directories, but the default development behavior should remain rooted at `arvane_home/`.

Code should resolve runtime paths through the central filesystem/path utility rather than hardcoding root-relative strings such as `assets/`, `config/`, `plugins/`, or `saves/`.

## Configuration Search Order

When loading configuration or data files, Arvane should use the following search order:

1. Explicit path provided by command-line argument
2. Explicit path provided by runtime configuration
3. Default path inside `arvane_home/`
4. Built-in default values

For example, when resolving the runtime home and default config file:

```text
--arvane-home <path>
runtime config paths.arvane_home
./arvane_home/
```

```text
--config <path>
runtime config paths.config_file
./arvane_home/config/arvane.yaml
./arvane_home/config/client.yaml
./arvane_home/config/server.yaml
built-in fallback behavior
```

If no explicit path is provided, the engine should assume:

```text
arvane_home/
```

relative to the resolved project root. Development builds stamp the source
project root into the runtime path resolver and prefer that root when it
contains the canonical `arvane_home/config/blocks.json` and shader assets. This
keeps deleted or stale build directories from becoming the asset root when an
executable is launched from an unusual working directory.

## Default Policy

Defaults should be safe, deterministic, and development-friendly.

If optional configuration is missing, the engine may fall back to built-in defaults.

If required configuration is missing, the engine should fail clearly with an actionable error message.

A good error should include:

* what was being loaded
* which path was attempted
* why the load failed
* what the user can do to fix it

Example:

```text
Failed to load block registry.
Attempted path: arvane_home/config/blocks.json
Reason: directory does not exist.
Fix: populate arvane_home/config/ or pass --arvane-home <path>.
```

The engine should not silently continue with partially loaded core data unless the missing data is explicitly optional.

## Required Versus Optional Data

Core gameplay data should be treated as required once the corresponding system exists.

Examples of required data:

* block registry definitions
* required shader sources
* required font files for the default UI
* required protocol or registry compatibility metadata

Examples of optional data:

* debug UI layouts
* development-only test worlds
* optional resource packs
* extra fallback fonts
* optional plugin assets

When unsure, systems should prefer explicit failure over hidden fallback behavior.

---

## Development and Test Behavior

Tests should not depend on the developer’s local working directory unless explicitly testing path discovery.

Unit and integration tests should prefer temporary directories or explicit fixture paths.

Test assets should live in a dedicated location such as:

```text
tests/assets/
```

or another clearly documented test fixture directory.

The production asset search policy and test fixture policy should remain separate so that tests do not accidentally depend on the full development asset tree.

## Summary

Arvane’s tooling stack is intentionally minimal and focused:

* **Language:** C++20
* **Build system:** CMake
* **Rendering:** OpenGL 3.3 Core + GLAD
* **Windowing/Input:** GLFW
* **ECS:** EnTT
* **Networking:** GameNetworkingSockets
* **Task system:** Taskflow
* **Concurrency primitives:** C++ standard library
* **Image loading:** stb_image
* **Text rendering:** FreeType + HarfBuzz
* **Atlas packing:** rectpack2D
* **Serialization:** nlohmann/json + yaml-cpp

This foundation provides a balance of performance, control, and maintainability suitable for a modern voxel engine.
