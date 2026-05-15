# UI Types

## Purpose

This document defines the major UI categories used by Arvane.

Arvane has two UI layers:

1. **Player UI**
   - Built with the Arvane retained UI system.
   - Used for gameplay-facing menus and HUD elements.

2. **Debug UI**
   - Built with ImGui.
   - Used only for development, diagnostics, testing, and editor-style tools.

Player-facing UI and debug UI must remain separate. ImGui should not be used for normal gameplay UI.

---

# Player UI

Player UI is rendered through the Arvane UI system.

These interfaces are part of the normal game experience and should support styling, layout, input focus, server-driven updates where appropriate, and plugin-defined extensions where allowed.

## Hotbar

The hotbar is the primary quick-access item UI.

It should display:

* selected slot
* item icons
* stack counts
* durability or usage indicators when applicable

The hotbar is visible during normal gameplay unless hidden by a full-screen menu or explicit UI state.

The selected slot is controlled by player input, such as number keys or mouse wheel scrolling.

---

## Inventory

The inventory is the main player item management UI.

By default, the inventory is opened with: `E`

The key binding must come from the configurable key bind system, not from a hardcoded key check.

The inventory should support:

* player inventory slots
* hotbar slots
* item dragging
* item splitting
* stack merging
* equipment slots if the game mode requires them
* server-authoritative item updates

Opening the inventory should place the player into UI focus mode so mouse and keyboard input are routed to the UI instead of the world.

---

## Chat

The chat UI is used for player communication and system messages.

By default, the chat is opened with: `T`

It should support:

* recent message history
* typed input
* command input
* scrolling
* fading inactive messages
* server-sent rich text messages
* colored text runs
* hover tooltips
* clickable text actions
* local client messages where appropriate

The chat input should capture keyboard focus while open.

When chat is closed, recent on-screen messages should render inside the same
chat background surface used by the open chat view. If no messages are currently
visible, the passive chat surface should not be drawn. Passive messages may
fade out after a configured duration and be capped to a configured number of
visible lines without being removed from retained chat history.

Network-visible chat is server-authored. Normal player input sends raw text to
the server, then the client displays the returned rich text payload. Player text
must remain literal unless the server applies formatting.

Supported chat interactions include `ShowText` hover tooltips, command
suggestion clicks, command-run clicks through the normal command path, and safe
URL confirmation for link clicks. Unsupported actions should fail quietly or
produce a local informational message rather than performing unsafe platform
behavior.

---

## Tab Menu

The tab menu displays online player information.

It is shown while the configured tab-list key is held.

The tab menu should display:

* online players
* optional player latency/ping
* optional player icons or avatars
* optional header text
* optional footer text

Header and footer content should be server-controlled when multiplayer is active.

The tab menu should be treated as a temporary overlay, not a full focus-stealing menu.

---

## Scoreboard

The scoreboard is an optional HUD panel displayed when scoreboard data is present.

The scoreboard should be intentionally simpler than Minecraft's scoreboard system.

A scoreboard consists of:

* ordered text lines
* optional blank spacer lines
* variables interpolated into line text
* built-in Arvane variables
* custom variables defined by game logic, plugins, or server state

Example:

```yaml
scoreboard:
  lines:
    - "Line one, this will appear at the top"
    - ""
    - "Health: {player_health}"
    - "Custom var: {custom_var}"

  vars:
    custom_var:
      type: int
      default: 10
      scope: player
```

## Scoreboard Lines

Each scoreboard line is a string.

Empty strings are valid and should render as spacer lines.

Variables are referenced using braces:

```text
{player_health}
{custom_var}
```

If a variable is missing, the renderer should not crash. The current shared text variable resolver preserves the original placeholder, such as `{missing_name}`, so missing values are visible without inventing misleading data.

---

## Scoreboard Variables

Scoreboard variables use the same `arv_text` variable resolver as chat message templates. Variables may be provided by Arvane, server state, game logic, or plugins.

Built-in variables currently include:

* `player_health`
* `player_max_health`
* `player_name`
* `world_name`
* `online_player_count`
* `tps`

Do not display fake values for unavailable data such as ping or dimension name. Add those variables only when authoritative sources exist.

Custom variables may be declared under `vars` or provided by plugins/server systems through the shared variable store.

Supported variable types should include:

* `int`
* `float`
* `bool`
* `string`

Variable scope should be explicit.

```yaml
scope: player
```

Means the variable is stored per player.

```yaml
scope: global
```

Means the variable is shared globally for the scoreboard instance.

The default scope should be:

```yaml
scope: global
```

Variable binding should also be explicit when it is not the default.

```yaml
binding: live
```

`resolved` is the default binding. Resolved variables are expanded by the
server before a chat message or scoreboard update is sent. Live variables are
replicated to the client and resolved when the client renders the UI text.

Use `scope: player` only when the value must differ per player.

---

# Debug UI

Debug UI is built with ImGui.

By default, the inventory is opened with: ` (grave)

Debug UI is not part of the normal player-facing UI system.

It exists for engine development, game testing, diagnostics, and internal tools.

## Debug Menu Structure

Arvane should use one compact, organized debug menu instead of many unrelated floating debug windows.

The debug menu may use:

* tabs
* collapsible sections
* tables
* filters
* search fields

The goal is to keep development tools discoverable without cluttering the screen.

Example tab categories:

* Player
* World
* Chunks
* Rendering
* Items
* Entities
* Networking
* Performance
* Profiling
* Console

---

## Debug Menu Tools

The debug menu should include tools such as:

* item adder
* game mode switcher
* teleport controls
* time-of-day controls
* weather controls if weather exists
* chunk reload controls
* chunk boundary visualization toggle
* collision/AABB visualization toggle
* lighting debug views
* render statistics
* entity inspector
* block inspector
* network statistics
* server tick statistics
* task/job system statistics
* Tracy profiling status and zone coverage
* frame p50/p95/p99 timelines
* meshing, lighting, and job queue latency percentiles
* memory/resource counters
* log viewer
* command runner

These tools should call into normal engine/game systems where possible instead of bypassing rules.

For example, the item adder should request an inventory mutation through the same authority path used by gameplay logic, rather than directly editing random inventory memory.

---

## Item Adder

The item adder is a debug tool for quickly inserting items into the player inventory.

It should support:

* searching items by name
* selecting stack size
* adding to inventory
* adding to hotbar
* optionally replacing selected slot

The item adder should respect the registry system and use stable item/block names, not hardcoded runtime IDs.

---

## Game Mode Switcher

The game mode switcher allows quickly changing the current player's game mode during development.

Possible game modes may include:

* survival
* creative
* spectator
* debug

The exact game mode list belongs to the gameplay rules, but the debug UI should expose whatever modes are registered.

Changing game mode should go through the same authority model used by commands or server-side gameplay state.

---

# Separation Rules

Player UI and debug UI must not be mixed.

Player UI:

* uses the Arvane UI system
* is suitable for shipping gameplay
* can be styled and themed
* can be driven by server/game state
* supports normal input focus rules

Debug UI:

* uses ImGui
* is development-only
* may expose unsafe or privileged tools
* should be easy to disable in release builds
* should not define gameplay-facing menus

Debug-only functionality must not become required for normal gameplay.
