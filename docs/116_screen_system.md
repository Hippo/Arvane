# UI Screen System & Command Routing

## Purpose

This document defines how UI screens (menus) are structured, how they are navigated, and how they communicate with the rest of the engine.

The goals are:

* prevent UI ↔ engine coupling
* keep menus modular and maintainable
* ensure consistent navigation behavior
* provide a clean command/event boundary

This system applies to the base-game/player-facing screens implemented in
`arv_base` on top of the generic `arv_ui` retained UI engine.

---

# 1. Module Boundaries

## Ownership

```text
arv_ui        → retained UI tree, layout, hit testing, drawing commands, themes
arv_base      → base-game screens, HUD widgets, menu navigation, UI commands
arv_client    → handles UI commands and executes actions
arv_world     → simulation (never directly touched by UI)
```

The source files live under `engine/arv_base`, but client UI code is built as
the separate `arv_base_ui` target so dedicated/server-only users of `arv_base`
do not link the UI stack.

## Core Rule

> UI must **never directly call into world, networking, or rendering systems**.

Instead:

```text
UI → emits command → handled externally
```

---

# 2. Screen System Overview

## What is a Screen?

A **screen** represents a full UI state:

* Main Menu
* Settings Menu
* Pause Menu
* Create World Screen
* Multiplayer Connecting Screen
* Multiplayer Disconnect Screen

Each screen:

* defines layout
* handles UI interaction
* emits commands
* does NOT execute gameplay logic

The multiplayer connecting and disconnect screens follow the same rule. The
connecting screen displays pending connection state and emits
`CancelServerConnectCommand`; the disconnect screen displays the client-provided
reason and emits `JoinServerCommand` for reconnect. The desktop/session layer
owns teardown, cancellation, connection startup, and the transition into world
rendering.

---

## Base Interface

```cpp
class UIScreen {
public:
    virtual ~UIScreen() = default;

    // Build layout every frame (retained-mode)
    virtual void build(UIContext& ctx) = 0;

    // Optional lifecycle hooks
    virtual void on_enter() {}
    virtual void on_exit() {}
};
```

---

# 3. Screen Manager

## Purpose

Controls which screen(s) are active and manages navigation.

## Responsibilities

* push/pop/replace screens
* maintain navigation stack
* route input focus
* ensure correct lifecycle calls

---

## Interface

```cpp
class UIScreenManager {
public:
    void push(std::unique_ptr<UIScreen> screen);
    void pop();
    void replace(std::unique_ptr<UIScreen> screen);

    UIScreen* current();

    void update(UIContext& ctx);
};
```

---

## Behavior

### Push

```text
push(A → B)
```

* B becomes active
* A remains in stack (paused underneath)

---

### Pop

```text
pop(B → A)
```

* B is destroyed
* A resumes

---

### Replace

```text
replace(A → B)
```

* A is destroyed
* B becomes active

---

## Stack Example

```text
Main Menu
  → Single Player Menu
    → Create World
```

Stack:

```text
[MainMenu, SinglePlayer, CreateWorld]
```

---

# 4. Navigation Rules

## Back Behavior

* `Back` button → `pop()`
* `Escape` key:

  * acts as `Back`
  * except when:

    * text input is focused (first clears focus)

---

## Root Screen

* Main Menu is the root
* Cannot be popped
* Only replaced (e.g., entering game)

---

## Pause Menu

* Pushed on top of gameplay UI state
* Does NOT destroy underlying state

---

# 5. UI Command System

## Purpose

Decouple UI from engine logic.

UI emits commands. Another system handles them.

---

## Command Definition

Use a variant or tagged struct system:

```cpp
struct CreateWorldCommand {
    std::string name;
    std::string seed;
};

struct JoinServerCommand {
    std::string address;
};

struct QuitGameCommand {};

using UICommand = std::variant<
    CreateWorldCommand,
    JoinServerCommand,
    QuitGameCommand
>;
```

---

## Command Sink

`arv_ui` should not process commands. Base-game screens in `arv_base` emit
commands that the app/client layer handles externally.

Instead, expose a sink:

```cpp
class UICommandSink {
public:
    virtual void submit(UICommand cmd) = 0;
};
```

---

## UIContext Extension

```cpp
struct UIContext {
    UICommandSink* command_sink;

    // existing UI state...
};
```

---

## Example Usage (Inside Screen)

```cpp
if (Button("Create")) {
    ctx.command_sink->submit(CreateWorldCommand{name, seed});
}
```

---

# 6. Command Handling (arv_client)

## Responsibility

`arv_client` (or app layer) receives commands and executes them.

---

## Example

```cpp
void Client::handle(UICommand cmd) {
    std::visit([&](auto&& c) {
        handle_command(c);
    }, cmd);
}
```

---

### Example Handlers

```cpp
void Client::handle_command(const CreateWorldCommand& cmd) {
    world_system.create_world(cmd.name, cmd.seed);
}

void Client::handle_command(const JoinServerCommand& cmd) {
    network.connect(cmd.address);
}

void Client::handle_command(const QuitGameCommand&) {
    app.quit();
}
```

---

# 7. Screen Lifecycle

## Hooks

```cpp
on_enter()
on_exit()
```

---

## When Called

| Action  | Behavior                   |
| ------- | -------------------------- |
| push    | new.on_enter               |
| pop     | old.on_exit                |
| replace | old.on_exit + new.on_enter |

---

## Use Cases

* reset form fields
* load data (world list, server list)
* refresh UI state

---

# 8. Data Flow Rules

## UI Reads Data From:

* config system (settings, servers)
* save system (world list)
* client state (connection status)

---

## UI Must NOT:

* mutate world state directly
* access ECS directly
* perform file I/O inline
* perform networking inline

---

## Correct Pattern

```text
UI → Command → Client → System
```

---

# 9. Widget vs Screen Responsibilities

## Screens

* layout structure
* navigation
* high-level flow

---

## Widgets

* reusable components
* no navigation logic
* no engine logic

---

## Example Split

```text
Screen:
  MultiplayerMenuScreen

Widgets:
  ServerList
  Button
  TextBox
```

---

# 10. Focus & Input Handling

## Rules

* only one focused element at a time
* text input captures keyboard input
* UI blocks world input when active

---

## Priority

```text
Focused UI Element
→ Active Screen
→ Gameplay Input
```

---

# 11. Modal / Blocking UI

## Examples

* confirm delete world
* invalid input popup

---

## Behavior

* rendered on top of current screen
* blocks interaction beneath
* dismissed explicitly

---

## Implementation

Option A (simple):

```text
screen owns modal state
```

Option B (advanced):

```text
separate modal stack in UI system
```

---

# 12. Persistence Boundaries

## Stored Outside UI

```text
servers list        → config
settings            → config
world saves         → save system
```

---

## UI Role

* display
* edit
* validate
* submit changes via commands

---

# 13. Example Flow

## Create World

```text
CreateWorldScreen
    ↓ user clicks Create
emit CreateWorldCommand
    ↓
arv_client handles
    ↓
world system creates save + world
    ↓
UIScreenManager.replace(GameScreen)
```

---

# 14. Error Handling

## Rules

* validation errors handled in UI
* system errors reported back via state or events

---

## Example

```text
Invalid seed → UI error label
Disk failure → client → UI notification
```

---

# 15. Agent Notes

When implementing:

* follow module boundaries strictly
* do not shortcut command system
* do not let UI call engine systems directly
* prefer simple screen classes over over-engineered abstractions
* reuse widgets aggressively
* keep screens thin and declarative

If something feels like "UI is doing too much", it probably is — move it to `arv_client`.

---

# Final Summary

```text
Generic UI lives in:    arv_ui
Base screens live in:   arv_base/ui/screens
Navigation lives in:    arv_base UI screen manager
Commands flow to:       arv_client
World never sees UI
```

This separation is critical for long-term maintainability and multiplayer correctness.
