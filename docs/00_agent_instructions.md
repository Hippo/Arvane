# Agent Instructions

## Purpose

This document defines how a coding agent must behave when working on the Arvane voxel engine.

It does **not** define engine behavior — it defines **agent behavior**.

The goal is to ensure:

* architectural consistency
* predictable output
* minimal hallucination
* incremental, testable progress

---

## Core Principles

### 1. Documentation is the Source of Truth

* All behavior, architecture, and constraints are defined in the `/docs` directory
* The agent must **read all relevant documents before making changes**
* If code and documentation conflict:

  * **assume documentation is correct**
  * update code, not the docs (unless explicitly instructed)

---

### 2. Do Not Invent Architecture

* Do **not** introduce:

  * new systems
  * new patterns
  * new abstractions
  * new ownership models

Unless they are:

* explicitly defined in documentation
* or explicitly requested by the user

If something is missing:

> Leave a `TODO` with a clear explanation instead of guessing.

---

### 3. Respect Module Boundaries

The project is intentionally modular.

The agent must:

* only depend on modules that are explicitly allowed
* avoid circular dependencies
* not bypass interfaces to access internal data

Example:

* `arv_physics` must not depend directly on `arv_world`
* it should depend on an **interface** (as defined in docs)

---

### 4. Prefer Small, Composable Implementations

* Avoid large, monolithic files
* Prefer:

  * focused classes
  * small systems
  * clear responsibilities

Each file should have a **single clear purpose**

---

### 5. Follow the Defined Build and Tooling Rules

* Only use dependencies listed in:

  * [Building and Tooling](50_building_and_tooling.md)
* Do not introduce:

  * new libraries
  * new frameworks
  * new build steps

Unless explicitly instructed

---

### 6. Implement Milestones in Order

* Follow [milestones](20_milestones.md) strictly
* Do **not** skip ahead
* Do **not** partially implement future systems

Each milestone should:

* compile
* run (if applicable)
* include tests where defined

---

### 7. When Underspecified, Do Not Guess

If documentation does not fully define behavior:

* do not invent a solution
* do not assume “standard” behavior

Instead:

```cpp
// TODO(arvane):
// Missing specification for <system/behavior>
// Required to proceed: <what is unclear>
```

---

### 8. Preserve Deterministic and Testable Systems

When implementing systems:

* ensure logic can be tested without rendering
* avoid hidden global state
* keep systems deterministic where possible

Examples:

* world generation must be seed-based
* serialization must round-trip correctly

---

### 9. No Hidden Side Effects

* Functions should be explicit about:

  * inputs
  * outputs
  * mutations

Avoid:

* implicit global writes
* hidden dependencies

---

### 10. Threading Rules Must Be Respected

* Follow [Multithreading and Jobs](220_multithreading_and_jobs.md) strictly
* Do not:

  * mutate world data off the simulation thread
  * access unsafe data from worker threads

Examples:

* meshing uses **immutable snapshots**
* GPU uploads happen on the render thread

---

### 11. Use Strong Types and Conventions

* Follow [Core types and Conventions](60_core_types_and_conventions.md)
* Use:

  * strong ID types
  * defined math types
  * consistent coordinate systems

Do not:

* introduce raw integers where typed IDs are required

---

### 12. Logging and Errors

* Prefer explicit error handling over silent failure
* Use logging where appropriate
* Do not spam logs

---

## Implementation Workflow

For every task:

### Step 1: Read Relevant Docs

* Identify all related documents
* Understand constraints before writing code

---

### Step 2: Identify Scope

* Determine:

  * which module is responsible
  * which interfaces are involved

---

### Step 3: Implement Minimally

* Implement only what is required for the milestone
* Avoid speculative features

---

### Step 4: Add Tests (if applicable)

* Unit tests for:

  * math
  * serialization
  * deterministic systems

---

### Step 5: Validate Against Docs

* Ensure implementation matches:

  * architecture
  * threading rules
  * data flow

---

### Step 6: Summarize Work

At the end of each task, provide:

* what was implemented
* what docs were followed
* any TODOs added
* any deviations (must be justified)

---

## Checklist Compliance

The agent must follow the [checklist](10_agent_checklist.md) for **every task**.

This checklist defines the required workflow for:

* understanding documentation
* validating scope
* implementing changes
* verifying correctness

### Requirements

* The checklist must be treated as **mandatory**, not optional
* Every step must be considered before, during, and after implementation
* No code should be written without first completing the **“Before Writing Code”** section
* All outputs must satisfy the **“After Implementation”** requirements

### Enforcement

If any checklist step cannot be satisfied:

* stop implementation
* add a clear `TODO`
* explain the blocker in the final summary

---

> The checklist is the execution layer of this document.
> These instructions define *what to follow* — the checklist defines *how to follow it*.

---


## Forbidden Actions

The agent must **never**:

* invent undocumented systems
* ignore module boundaries
* introduce unapproved dependencies
* skip milestones
* implement large speculative systems
* silently deviate from documentation

---

## Allowed Flexibility

The agent **may**:

* improve code clarity
* refactor within module boundaries
* add small helper utilities (if local and justified)

---

## Output Expectations

All code must be:

* readable
* consistent
* modular
* aligned with documentation

---

## Summary

This project is **documentation-driven**.

The agent’s role is to:

> faithfully implement the documented design — not reinterpret or redesign it.

When in doubt:

> **Stop, document the uncertainty, and ask or leave a TODO.**

---


