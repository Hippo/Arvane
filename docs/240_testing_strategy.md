# Testing Strategy

## Purpose

This document defines the testing strategy for Arvane.

The goal of testing is not only to catch regressions, but to force clean module boundaries. Systems should be designed so that core behavior can be tested without opening a window, creating an OpenGL context, connecting to a network socket, or loading the full game client.

Arvane uses:

- GoogleTest for unit and integration tests
- GoogleMock for mocks, fakes, and interface boundary tests

Tests should be added whenever a system has behavior that can regress, especially when the behavior affects world correctness, serialization compatibility, networking, simulation, or math.

---

# Testing Principles

## Core Logic Must Be Testable Without Rendering

Most engine systems must be testable in a headless environment.

The following systems should not require rendering:

- core math utilities
- ID types and coordinate conversion
- chunk storage
- block registry loading and lookup
- chunk serialization
- region file encoding and decoding
- world generation
- lighting propagation logic
- liquid simulation rules
- physics collision checks
- raycasting
- inventory rules
- protocol packet encoding and decoding
- scheduler/job dependency logic

Rendering tests may exist separately, but rendering must not be required to validate core gameplay or world behavior.

---

## Prefer Deterministic Tests

Tests should avoid relying on:

- wall-clock time
- random device state
- unordered container iteration order
- actual thread timing
- GPU behavior
- network timing
- filesystem paths outside test fixtures

When randomness is required, tests must use explicit seeds.

World generation, simulation, and serialization tests must be repeatable across runs.

---

## Test Public Behavior, Not Private Implementation

Tests should validate observable behavior and contracts.

Avoid writing brittle tests against internal implementation details unless the internal detail is itself a documented contract, such as:

- binary save layout
- packed vertex format
- packet encoding
- chunk coordinate conversions
- block/state ID invariants
- lighting byte layout

---

# Test Framework

Arvane uses GoogleTest and GoogleMock.

GoogleTest is used for:

- unit tests
- integration tests
- fixture-based tests
- parameterized tests
- death tests only when appropriate

GoogleMock is used for:

- mocking interfaces between modules
- validating adapter boundaries
- replacing world/block/query interfaces in physics tests
- replacing transport interfaces in networking tests
- testing systems that depend on callbacks or service interfaces

GoogleMock should not be used to over-specify implementation order unless ordering is part of the contract.

---

# Test Target Structure

Tests should be organized by module.

Example test targets:

```text
arv_core_tests
arv_math_tests
arv_world_tests
arv_blocks_tests
arv_meshing_tests
arv_lighting_tests
arv_simulation_tests
arv_physics_tests
arv_save_tests
arv_protocol_tests
arv_networking_tests
arv_assets_tests
arv_ui_tests
```

Not every module needs a test target immediately, but the build system should make it easy to add one.

Each test target should link only the modules it needs.

A test for `arv_core` must not pull in rendering, networking, or world systems.

---

# Unit Tests

Unit tests validate small, isolated pieces of behavior.

Good unit test candidates include:

* coordinate conversion
* chunk index calculation
* region coordinate calculation
* strongly typed ID behavior
* packed data encoding and decoding
* math utility functions
* block registry lookup
* state ID mapping
* face visibility predicates
* light channel packing
* fluid level helpers
* AABB intersection
* raycast stepping
* inventory stack merging
* packet field encoding

Unit tests should be fast and should not require real asset files unless the file itself is a small test fixture.

---

# Integration Tests

Integration tests validate the boundaries between multiple systems.

Good integration test candidates include:

* loading block definitions and building registries
* generating a chunk and meshing it
* updating a chunk and verifying dirty flags
* simulating liquid spread across chunk boundaries
* propagating light across chunk boundaries
* serializing and deserializing chunk data
* encoding and decoding protocol packets
* applying player input to physics movement
* loading assets into CPU-side resource structures

Integration tests may use temporary directories and small fixture worlds.

They should still avoid opening a window or requiring OpenGL unless explicitly testing rendering.

---

# Rendering-Independent Testing

The following must be designed so they can run without rendering:

## Chunk Meshing

Chunk meshing should be testable by giving the mesher:

* chunk block data
* neighbor boundary data
* lighting data
* material/texture metadata when needed

The test should inspect CPU-side mesh output:

* vertex count
* index count
* face count
* opaque vs transparent split
* packed vertex fields
* UV metadata
* lighting values
* AO values

Tests must not require GPU uploads.

## Lighting

Lighting tests should validate:

* channel packing
* sunlight propagation
* block light propagation
* removal behavior
* cross-chunk border behavior
* dirty region scheduling

Lighting propagation should be testable against in-memory chunks.

## Physics

Physics tests should use mocked or fake terrain query interfaces.

`arv_physics` should not need a real world object. It should only need an interface capable of answering queries such as whether a block position is solid.

Use GoogleMock when testing that physics interacts with an abstract terrain interface correctly.

Use simple fakes when a small in-memory block map is easier and clearer.

---

# Deterministic World Generation Tests

World generation must support deterministic tests.

Tests should verify that:

* the same seed produces the same chunk output
* different seeds can produce different output
* generation does not depend on chunk load order
* generation at chunk borders is consistent
* biome or terrain transitions are stable
* generated block states are valid registry states

World generation tests should use small sample regions and explicit seeds.

Example cases:

```text
Seed 1234, chunk (0, 0, 0) produces stable block/state distribution.
Seed 1234, neighboring chunks agree along shared borders.
Generating chunk A before chunk B produces the same result as generating chunk B before chunk A.
```

Large snapshot tests should be avoided unless the snapshot format is intentionally stable.

Prefer validating important invariants over storing huge expected outputs.

---

# Serialization Round-Trip Tests

Serialization must have round-trip tests.

Any persisted format should support tests like:

```text
original data -> serialize -> deserialize -> compare with original
```

Round-trip tests should cover:

* chunk block data
* chunk lighting data
* local palette tables
* region headers
* player save data
* inventory data
* entity persistence data
* protocol packet encoding
* registry name references

Serialization tests should also cover invalid or corrupted input when practical.

Binary format tests must be strict where the layout is documented.

The region file format should include tests for:

* magic value
* version
* header size
* chunk offset table
* chunk length table
* compression flags if used
* missing chunk handling
* invalid offset handling
* truncated data handling

Runtime IDs must not be assumed stable across saves unless a specific remapping contract exists.

---

# Math Utility Tests

Math utilities must be heavily unit tested because they are used across many systems.

Test candidates include:

* integer floor division
* modulo behavior for negative coordinates
* world-to-chunk conversion
* world-to-local-block conversion
* chunk-to-region conversion
* AABB overlap
* vector normalization edge cases
* ray direction stepping
* fixed tick time conversion
* interpolation helpers
* packed integer bit operations

Negative coordinate behavior is especially important.

Voxel engines commonly fail at chunk and region boundaries when coordinates become negative.

---

# Mocking and Fakes

Use mocks for interface contracts.

Use fakes for data-heavy systems.

## Prefer Fakes When

* the dependency is simple
* the test needs many queries
* behavior is easier to express as data
* strict call ordering does not matter

Example:

* fake chunk storage
* fake block registry
* fake terrain collision map
* fake resource loader

## Prefer GoogleMock When

* testing an interface boundary
* verifying a callback is invoked
* verifying error paths
* validating ownership or handoff behavior
* replacing network transport
* replacing scheduler/job submission

Mocks should not make tests fragile by requiring incidental call order.

---

# Filesystem Tests

Filesystem tests must use temporary test directories.

Tests must not write to real user save directories.

Save tests should create isolated test fixtures and clean them up after completion.

Filesystem tests should cover:

* creating new save directories
* loading missing optional files
* rejecting invalid required files
* writing region files
* reading region files
* replacing files atomically if the save system supports it

---

# Networking and Protocol Tests

Protocol tests should not require real sockets.

Packet encoding and decoding should be tested independently from transport.

Tests should cover:

* packet IDs
* packet length validation
* malformed packets
* unknown packet handling
* version negotiation data
* registry synchronization data
* chunk payload encoding
* entity update encoding

Networking integration tests may use fake transports or loopback transports.

GameNetworkingSockets-specific tests should be isolated from protocol tests.

---

# Scheduler and Threading Tests

Threading tests must avoid relying on timing whenever possible.

The scheduler should expose testable primitives for:

* job dependency ordering
* completion tracking
* cancellation
* main-thread handoff
* worker result publication

Tests should validate ownership rules where possible.

Examples:

* meshing jobs consume immutable snapshots
* mesh publication rejects stale source versions
* GPU upload requests are marshaled to the render thread queue
* world mutation jobs are not allowed from worker contexts

Race-condition tests are useful, but they should not be the only validation method.

---

# Test Fixtures

Test fixtures should be small, focused, and checked into the repository.

Good fixtures include:

* minimal block registry YAML files
* tiny chunk save files
* intentionally corrupted save files
* protocol packet byte arrays
* small font or asset metadata fixtures if needed
* minimal world generation configs

Fixtures should be versioned alongside the tests that use them.

Do not use large binary fixtures unless necessary.

---

# Continuous Testing Expectations

The normal test suite should be fast enough to run frequently during development.

Recommended categories:

```text
unit
integration
serialization
slow
rendering
network
```

The default test run should include unit and core integration tests.

Slow, rendering, and real-network tests should be opt-in.

---

# CMake Expectations

Testing must be integrated into CMake using CTest.

Each test target should be registered with `add_test`.

Tests should be runnable with:

```sh
ctest --test-dir build
```

or from the build directory:

```sh
ctest
```

GoogleTest and GoogleMock should be linked through CMake targets rather than manually adding include paths.

Expected targets:

```cmake
GTest::gtest
GTest::gtest_main
GTest::gmock
GTest::gmock_main
```

Use `gtest_discover_tests` when practical.

---

# What Should Not Be Tested First

Avoid spending early effort on fragile tests for:

* exact rendered pixels
* exact GPU driver behavior
* real network latency
* full game startup
* giant end-to-end scenarios
* screenshots
* platform-specific window behavior

These can be added later, but the first priority is validating engine correctness and module boundaries.

---

# Required Early Test Coverage

Early milestones should include tests for:

* core coordinate conversion
* chunk indexing
* block/state registry invariants
* Air state invariants
* deterministic world generation seed behavior
* serialization round trips
* region file header validation
* math helpers
* raycast stepping
* basic AABB collision
* protocol packet round trips

These tests should be added as the corresponding systems are implemented, not deferred until the end.

---

# Agent Rules

Coding agents must follow these rules:

* Add tests when implementing behavior that has clear expected outcomes.
* Do not require rendering for world, simulation, physics, save, or protocol tests.
* Use GoogleTest for test cases.
* Use GoogleMock for mocked module boundaries.
* Prefer deterministic tests over timing-dependent tests.
* Prefer small fixtures over large snapshots.
* Do not introduce a new test framework.
* Do not skip tests because a system is “temporary” if the behavior is documented.
* When a behavior is underspecified, add a TODO and avoid inventing hidden test assumptions.
* Keep tests close to the module they validate.
* Ensure tests can run through CTest.
