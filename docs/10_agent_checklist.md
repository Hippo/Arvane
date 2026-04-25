# Agent Checklist

## Purpose

This checklist must be followed for **every implementation task**.

It ensures:

* consistency with documentation
* correct architecture usage
* minimal hallucination
* clean, incremental progress

---

## Before Writing Code

### 1. Read Relevant Documentation

* [ ] Have all relevant docs been read?
* [ ] Are constraints clearly understood?
* [ ] Are threading and ownership rules known?

---

### 2. Confirm Scope

* [ ] Which module owns this responsibility?
* [ ] Are module boundaries respected?
* [ ] Are dependencies valid per [building and tooling](50_building_and_tooling.md)?

---

### 3. Check Milestone Alignment

* [ ] Is this part of the **current milestone**?
* [ ] Am I avoiding future systems?
* [ ] Is the implementation minimal?

---

### 4. Identify Unknowns

* [ ] Is anything underspecified?
* [ ] If yes:

  * [ ] Add a `TODO` instead of guessing
  * [ ] Clearly describe what is missing

---

## During Implementation

### 5. Follow Architecture Strictly

* [ ] No new systems introduced
* [ ] No undocumented abstractions
* [ ] Interfaces respected

---

### 6. Respect Threading Model

* [ ] No unsafe cross-thread access
* [ ] Worker jobs use immutable data
* [ ] World mutation only on simulation thread

---

### 7. Keep Code Small and Focused

* [ ] Files have a single responsibility
* [ ] No large monolithic classes
* [ ] Logic is composable

---

### 8. Use Correct Types and Conventions

* [ ] Strong ID types used where required
* [ ] No raw integers for IDs
* [ ] Coordinate systems consistent

---

### 9. Avoid Hidden Behavior

* [ ] No hidden global state
* [ ] No implicit side effects
* [ ] Inputs/outputs are clear

---

## After Implementation

### 10. Validate Against Documentation

* [ ] Matches architecture docs
* [ ] Matches threading rules
* [ ] Matches data flow expectations

---

### 11. Add Tests (If Applicable)

* [ ] Unit tests added where appropriate
* [ ] Deterministic systems are testable
* [ ] Serialization round-trip verified (if relevant)

---

### 12. Ensure Build Integrity

* [ ] Project compiles
* [ ] No new dependencies added
* [ ] Build system unchanged unless required

---

### 13. Review for Scope Creep

* [ ] No extra features added
* [ ] No speculative systems included
* [ ] Implementation is minimal and focused

---

## Final Output Requirements

Every task must end with:

### Summary

* What was implemented
* Which documents were followed

### TODOs

* List all unresolved or underspecified areas

### Deviations (if any)

* Clearly explain:

  * what changed
  * why it was necessary

---

## Hard Stop Conditions

The agent must stop and not proceed if:

* Required documentation is missing
* Behavior is unclear or ambiguous
* Implementation would require guessing architecture

Instead:

> Add a clear `TODO` and explain the blocker.

---

## One-Line Rule

> **Do not guess. Do not invent. Follow the docs exactly.**

---
