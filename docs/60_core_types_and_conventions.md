# Core Types and Conventions

## Purpose

This document defines the fundamental type, naming, ownership, error-handling, memory, and coordinate conventions used across the Arvane codebase.

This is a foundational document. Its purpose is to eliminate subtle inconsistencies between modules, contributors, and coding agents.

---

## Design Goals

- Prevent domain confusion (especially IDs and coordinates)
- Make ownership and lifetime obvious
- Keep APIs explicit and predictable
- Favor correctness first, then performance
- Enable safe large-scale refactoring

---

## 1. Integer and Primitive Type Aliases

### Canonical aliases

```cpp
using i8  = std::int8_t;
using i16 = std::int16_t;
using i32 = std::int32_t;
using i64 = std::int64_t;

using u8  = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t;

using f32 = float;
using f64 = double;

using usize = std::size_t;
using isize = std::ptrdiff_t;
```

### Rules

* Use fixed-width types for all engine-facing APIs.
* Avoid `int`, `long`, etc. in interfaces.
* Use `usize` for container sizes and indexing.
* Do not use unsigned types just to “prevent negatives”.

---

## 2. Strong ID Types (CRITICAL)

### Principle

**Raw integers must not be used for domain identities.**

All IDs that cross system boundaries must use **strongly typed ID wrappers**.

---

### Base ID type

```cpp
template <typename Tag, typename ValueType = u32>
struct ID {
    using value_type = ValueType;

    ValueType value{};

    constexpr ID() = default;
    constexpr explicit ID(ValueType v) : value(v) {}

    constexpr bool operator==(const ID&) const = default;
    constexpr auto operator<=>(const ID&) const = default;
};
```

---

### Example usage

```cpp
struct BlockTag {};
using BlockID = ID<BlockTag>;

struct EntityTag {};
using EntityID = ID<EntityTag>;

struct WorldTag {};
using WorldID = ID<WorldTag>;
```

---

### Rules

* IDs from different domains must never be interchangeable.
* Never use raw integers where a domain-specific ID exists.
* ID wrappers must remain:

  * trivial
  * zero-cost
  * POD-like
* Do not add heavy logic to ID types.

---

### Hash support

All ID types must be hashable:

```cpp
template <typename Tag, typename ValueType>
struct std::hash<ID<Tag, ValueType>> {
    usize operator()(const ID<Tag, ValueType>& id) const noexcept {
        return std::hash<ValueType>{}(id.value);
    }
};
```

---

### Validity rules

There is **no global invalid ID rule**.

Each system defines its own semantics:

* `BlockID(0)` = Air (valid)
* `EntityID(0)` may be invalid
* Others may use sentinel values

Do **not** assume `0` means invalid.

---

### When to use strong IDs

Use for:

* Block IDs
* Entity IDs
* Item IDs
* World/Dimension IDs
* Player/session IDs
* Any cross-system identifier

Do NOT use for:

* indices
* counters
* array offsets
* local palette indices

---

## 3. Math Types

### Core types

* `Vec2i`, `Vec3i`, `Vec4i`
* `Vec2f`, `Vec3f`, `Vec4f`
* `Vec3d` (precision-sensitive cases)
* `Mat4f`
* `Quatf`
* `Aabb3f`

---

### Rules

* Integer vectors (`Vec3i`) for voxel/grid logic
* Float vectors (`Vec3f`) for rendering and transforms
* Do not mix integer and float coordinate spaces implicitly

---

## 4. Naming Conventions

### Types

`PascalCase`

* `BlockRegistry`
* `ChunkSection`

---

### Functions & variables

`snake_case`

* `load_chunk`
* `block_id`

---

### Private member variables

Trailing underscore:

```cpp
capacity_
world_id_
```

---

### Constants

`UPPER_CAMEL_CASE`

* `CHUNK_SIZE`
* `MAX_LIGHT_LEVEL`

---

### Namespaces

`snake_case`

* `arv::core`
* `arv::world`

---

### Enum variants
`PascalCase`

```cpp
enum class Direction {
  North, South, East, West
};
```

## 5. Ownership and Lifetime

### Rules

* Ownership must be explicit in APIs
* Raw pointers are **non-owning**
* References are **non-null**
* Use `unique_ptr` for ownership
* Avoid `shared_ptr` unless necessary

---

### Vocabulary

* **owning**
* **borrowed**
* **view**
* **handle**
* **registry-owned**
* **world-owned**

---

### Subsystem ownership

* World owns simulation state
* Global systems own cross-world state
* Registries own definitions and mappings

---

## 6. Error Handling

### Rules

* Exceptions are NOT the default
* Use:

  * `Expected<T, E>`
  * `optional<T>`
* Assertions for programmer errors only

---

### Examples

Use result types for:

* asset loading
* parsing
* network failures
* registry lookup failures

---

## 7. Memory Rules

### Principles

* Avoid allocations in hot paths
* Prefer contiguous storage
* Separate hot vs cold data
* Reuse buffers

---

### Avoid

* per-frame heap churn
* `shared_ptr` in tight loops

---

### Use

* `std::span` for views
* stable handles when needed

---

## 8. Coordinate System (CRITICAL)

### Orientation

* **Right-handed**
* **Y-up**

---

### Axes

* +X: east/right
* +Y: up
* +Z: forward

---

## Coordinate spaces

### World space

* `Vec3f` / `Vec3d`
* continuous

---

### Block space

* `Vec3i`
* discrete voxel grid

---

### Section space

* local 16×16×16 region

---

### Chunk space

* horizontal chunk coordinates or full 3D chunk coords (must be explicit per system)

---

### Rules

* Never mix coordinate spaces implicitly
* Always use helper functions for conversion
* Must handle negative coordinates correctly

---

## 9. Runtime IDs vs Authored Names

### Principle

Separate:

* **Stable authored identity**
* **Runtime representation**

---

### Authored names

* `arvane:stone`
* stable, human-readable

---

### Runtime IDs

* compact numeric IDs
* generated at load
* not stable across runs

---

### Rules

* Never hardcode runtime IDs
* Systems may use them internally
* APIs should prefer names or handles where stability matters

---

### Reserved values

* `BlockID(0)` = Air

---

## 10. Nullability

### Rules

* Reference = required
* Pointer = optional
* Use `optional` for return values

---

## 11. Const Correctness

* Prefer immutable inputs
* Use `const` aggressively
* Mutation must be explicit

---

## 12. Threading Baseline

* Do not share mutable state without structure
* Align ownership with threading boundaries
* Prefer job-based data flow

---

## 13. API Boundaries

### Prefer

* strong types (IDs, vectors)
* spans
* explicit results

---

### Avoid

* raw integers for IDs
* ambiguous ownership
* stringly-typed APIs

---

Here’s a rewritten version that keeps your structure but makes `arv::FunctionRef` a first-class concept (same level of clarity as `arv::HashMap`):

---

## 14. Container & Callable Policy

Arvane prefers standard library containers by default unless profiling,
data layout, or hot-path behavior justifies a specialized container.

For performance-sensitive hash maps and hash sets, use `arv::HashMap`
and `arv::HashSet` rather than spelling `std::unordered_map` or a third-party
container directly.

The default implementation of `arv::HashMap` / `arv::HashSet` may use a
high-performance open-addressing hash table such as `ankerl::unordered_dense`.

Use `arv::StableHashMap` when code depends on stronger reference, pointer, or
iterator stability semantics from `std::unordered_map`.

Hot ID-indexed data should prefer dense arrays/vectors over hash maps whenever
possible. For example, runtime block state tables, material tables, light tables,
and chunk-local arrays should generally be indexed by compact integer IDs.

---

## Callable Policy (`arv::FunctionRef`)

Arvane distinguishes between **borrowed (non-owning)** and **owning** callables.

### `arv::FunctionRef`

`arv::FunctionRef<R(Args...)>` is a lightweight, non-owning reference to a callable.

* **Zero allocation**
* **Very cheap to copy (two pointers)**
* **Does not own the callable**
* **Does not extend lifetime**

It should be used for **immediate, synchronous invocation** in hot paths.

```cpp
void for_each_entity(arv::FunctionRef<void(Entity)> fn);
```

```cpp
for_each_entity([&](Entity e) {
    // Executed immediately, no allocation
});
```

### Rules

`FunctionRef` is a *borrowed callable*. It must not outlive the callable it references.

#### ✔ Use `FunctionRef` when:

* The callable is invoked **immediately**
* The API does **not store** the callable
* The call is part of a **hot path** (ECS iteration, rendering, jobs, traversal)
* You would otherwise pass `std::function` **by const reference**

#### ✖ Do NOT use `FunctionRef` when:

* The callable must be **stored**
* The callable is used **asynchronously**
* The callable may **outlive the caller’s stack frame**

```cpp
// ❌ Incorrect: storing a FunctionRef
struct Task
{
    arv::FunctionRef<void()> fn;
};
```

---

### Owning Callables

When ownership is required, use an owning callable type.

```cpp
using JobFunction = std::function<void()>;
```

Use cases:

* job systems
* event queues
* deferred execution
* long-lived callbacks

This alias may later be replaced with a small-buffer optimized or move-only
callable without changing call sites.

---

## Preferred Performance Containers / Patterns

* Use `arv::HashMap` / `arv::HashSet` for hot hash tables.
* Use `arv::StableHashMap` when element address/iterator stability is required.
* Avoid `std::map` / `std::set` unless ordered iteration and stable node semantics are required.
* Prefer sorted vectors or hash maps for small/medium lookup tables.
* Avoid `std::list` and `std::forward_list` in engine/runtime code.
* Prefer dense vectors indexed by runtime IDs over associative containers.
* Avoid `std::function` in hot paths; prefer template callables, function pointers, or `arv::FunctionRef`.
* Avoid `std::shared_ptr` in hot paths; prefer explicit ownership, handles, indices, or arenas.

---

## 15. Summary of Critical Rules

* Use **strong ID types** for all domain identities
* Raw pointers are non-owning
* Use `unique_ptr` for ownership
* No default exception-based flow
* Right-handed, Y-up coordinate system
* Separate authored names from runtime IDs
* Do not assume `0` is invalid
* Never mix coordinate spaces implicitly

---
