# UI System

Arvane uses two separate UI paths:

- **Dear ImGui** for debug, diagnostics, profiling, editor-like tools, and developer-only UI.
- **Arvane UI** for all user-facing game UI.

Dear ImGui must not be used for normal player-facing screens such as inventory, menus, chat, settings, crafting, container screens, or plugin-provided interfaces.

The user-facing UI is an **engine-native retained UI system** with optional immediate-style authoring helpers for built-in C++ UI.

---

## Goals

The UI system must support:

- HUD overlays
- inventory and hotbar UI
- pause/settings screens
- chat and text entry
- tooltips
- container screens
- modal dialogs
- plugin-authored menus
- server-sent UI descriptions
- keyboard, mouse, and future controller navigation
- safe multiplayer UI interactions

The system should be simple enough to implement incrementally, but structured enough to support a real moddable game.

---

## Non-Goals

The initial UI system is not:

- a browser
- a CSS clone
- a JavaScript runtime
- an editor UI framework
- a replacement for Dear ImGui
- a system where plugins execute arbitrary client UI code
- a system where UI widgets directly mutate world state

---

## Core Design

User-facing UI is represented as a retained tree of elements.

Each element has:

- stable element ID
- element type
- parent/child relationship
- layout properties
- style token references
- visibility state
- enabled/disabled state
- focusability flags
- optional widget state
- optional semantic action bindings

The retained tree is the source of truth for:

- layout
- hit testing
- focus navigation
- rendering order
- input dispatch
- widget state

Built-in client UI may use immediate-style C++ authoring helpers, but those helpers must update the retained tree internally. They are not the runtime model.

---

## Architecture

```text
Platform input
    ↓
Client input system
    ↓
Debug ImGui capture check
    ↓
Arvane UI input router
    ↓
Focused / hovered / modal UI elements
    ↓
Semantic UI actions
    ↓
Client gameplay / networking layer
```

```text
UI tree
    ↓
Layout pass
    ↓
Paint command generation
    ↓
UI renderer
    ↓
Final screen output
```

The UI system owns:

* UI tree state
* layout
* focus
* hit testing
* input routing
* widget state
* semantic UI events
* paint command generation

The renderer owns:

* drawing quads
* drawing text
* clipping/scissor
* texture binding
* batching
* GPU submission

Widgets must not directly call graphics APIs.

---

## Screens

A screen is a top-level user-facing UI unit.

Examples:

* HUD
* main menu
* pause menu
* inventory
* crafting table
* chest/container
* settings
* chat overlay
* death screen
* plugin shop menu

Screen categories:

```text
HUD
  Persistent gameplay overlay.

Overlay
  Drawn over gameplay without necessarily blocking all input.

Fullscreen
  Owns the full viewport.

Modal
  Blocks interaction with screens behind it.
```

Screens must declare their input behavior:

```text
PassiveOverlay
  Does not block gameplay input.

CursorOverlay
  Shows cursor and allows UI interaction.

BlocksGameplay
  Blocks normal gameplay input.

Modal
  Blocks gameplay and lower UI screens.
```

---

## Input Modes

The client has explicit input modes:

```text
Gameplay
CursorUi
TextEntry
ModalUi
DebugUi
```

Input mode controls:

* cursor visibility
* cursor lock
* camera input
* gameplay hotkeys
* text input routing
* UI navigation behavior

The input mode must not be inferred only from whether a screen is open.

---

## Input Priority

Input is routed in this order:

```text
1. Platform/window handling
2. Dear ImGui debug capture, if enabled
3. Top modal Arvane UI screen
4. Focused Arvane UI screen
5. Hovered Arvane UI elements
6. Gameplay input
```

Gameplay receives only input that was not consumed by UI.

---

## Focus Model

The UI system tracks distinct focus concepts:

```text
hover
  Element currently under the cursor.

active
  Element currently pressed, dragged, or interacted with.

keyboard_focus
  Element receiving keyboard/text input.

mouse_capture
  Element receiving mouse movement/up after beginning an interaction.

navigation_focus
  Element selected by keyboard/controller navigation.
```

These must remain separate. A text input may have keyboard focus while a different element is hovered. A dragged item may capture the mouse while the cursor is outside its original bounds.

---

## Event Dispatch

UI events use capture and bubble phases:

```text
Capture:
  root → target

Bubble:
  target → root
```

Widgets may consume events. Consumed events must not be forwarded to gameplay input.

Event results include:

```text
Ignored
Handled
CaptureMouse
ReleaseMouse
RequestKeyboardFocus
ClearKeyboardFocus
```

---

## Mouse Input

Supported mouse events:

* move
* button down
* button up
* wheel
* enter element
* leave element

Rules:

* mouse down may set active element
* mouse down may request mouse capture
* mouse up releases active state unless interaction continues
* scroll events route to nearest scrollable ancestor
* hidden/disabled elements are not valid hit targets
* clipped regions must not receive input
* transparent visual elements may opt out of hit testing

---

## Keyboard and Text Input

Keyboard input must distinguish:

```text
Physical key events
  KeyDown / KeyUp

Text input events
  Unicode text after keyboard layout / IME processing
```

Text widgets consume text input events.

Special keys remain key events:

* Escape
* Enter
* Tab
* Backspace
* Delete
* arrow keys
* Home / End
* Page Up / Page Down
* modifier shortcuts

Gameplay actions must come from the input binding system, not raw key handling inside UI widgets.

---

## Escape Behavior

Escape follows global behavior:

```text
If keybind capture is active:
  cancel keybind capture

Else if text input is focused:
  clear keyboard focus

Else if top modal is closable:
  close top modal

Else if current screen is closable:
  close current screen

Else if in gameplay:
  open pause menu
```

Plugin/server-provided screens may request non-closable behavior, but the client should only honor this for trusted/system screens.

---

## Modal Stack

Modal UI uses a stack.

Only the top modal receives normal interaction.

Screens behind the top modal may render dimmed or inactive but must not receive pointer or keyboard events unless explicitly allowed.

Example:

```text
Pause Menu
  Settings Dialog
    Keybind Capture Popup
```

---

# Layout System

The UI system uses a deterministic retained layout model.

Layout computes:

* element position
* element size
* clipping bounds
* child placement

Layout must be:

* deterministic
* independent of rendering
* stable across frames given identical input
* free of side effects outside the UI tree

---

## Layout Phases

Layout occurs in two phases:

```text
1. Measure
2. Arrange
```

### Measure

Each element determines desired size based on:

* content
* children
* parent constraints
* style properties
* padding
* min/max size

### Arrange

Each element receives a final rectangle and positions its children within it.

Arrange handles:

* alignment
* spacing
* stacking order
* clipping bounds

---

## Core Layout Primitives

The initial system supports:

### Panel

A basic rectangular container.

* may apply padding
* may clip children
* does not impose child layout by itself

### Vertical Stack

Lays out children top-to-bottom.

Supports:

* spacing
* left alignment
* center alignment
* right alignment
* stretch alignment

### Horizontal Stack

Lays out children left-to-right.

Supports:

* spacing
* top alignment
* center alignment
* bottom alignment
* stretch alignment

### Grid

Places children in row-major order.

Supports:

* fixed column count
* configurable spacing
* uniform cell sizing initially
* variable cell sizing later if needed

### Absolute Layout

Positions children relative to parent bounds.

Supports anchors:

* top-left
* top-right
* bottom-left
* bottom-right
* center
* top-center
* bottom-center
* left-center
* right-center

### Scroll Area

Defines a clipped viewport with scrollable content.

Supports:

* vertical scrolling
* horizontal scrolling later
* scroll offset
* scroll wheel input
* scrollbar widget integration

### Overlay Layer

Renders children above normal layout flow.

Used for:

* tooltips
* popups
* drag previews
* context menus

Overlay elements do not affect sibling layout.

---

## Sizing Rules

Elements may define:

```text
Fixed size
  explicit width and/or height

Content size
  intrinsic size from text, image, or children

Fill
  expands to available parent space

Min / Max constraints
  bounds applied after size calculation
```

Rules:

* children must respect parent constraints
* layout must not produce negative sizes
* zero-size elements must be safe
* overflow behavior must be explicit: clip or allow
* layout must not depend on GPU state

---

## Spacing and Padding

Layout supports:

```text
Padding
  internal space inside an element

Spacing
  external distance between siblings
```

Padding affects child layout bounds.

Spacing affects stacks and grids.

---

## Clipping

Rules:

* clipping is hierarchical
* child clipping intersects with parent clipping
* clipping affects rendering
* clipping affects hit testing
* clipped regions must not receive input

---

## Hit Testing and Layout

Hit testing uses final layout bounds.

Rules:

* only visible and enabled elements participate
* clipped regions are ignored
* overlay elements may override normal hit order
* modal elements block lower layers
* disabled elements may still render but cannot be interacted with

---

## Layout Non-Goals

The initial layout system must not:

* implement full CSS
* implement flexbox
* use arbitrary constraint solvers
* allow cyclic layout dependencies
* mutate non-UI state
* depend on rendering state

Additional layout features should only be introduced when required by concrete use cases.

---

# Rendering Model

The UI system emits paint commands.

Paint command types include:

* filled rectangle
* bordered rectangle
* textured rectangle
* text run
* item icon
* nine-slice panel
* clipped group begin/end
* scissor begin/end
* debug bounds overlay

Rendering order:

```text
1. World
2. Entities
3. Transparent world pass
4. User-facing UI
5. Debug UI, if enabled
```

Debug UI may render above user UI during development.

---

## Text Rendering

The UI system requires engine-owned text rendering.

Requirements:

* font assets
* glyph atlas
* UTF-8 text
* text measurement
* line wrapping
* clipping
* alignment
* color
* scale
* future rich text spans

Text input stores UTF-8 strings.

---

# Styling

Widgets use style tokens rather than hardcoded values.

Example style token categories:

```text
panel background
button background
button hover
button active
text primary
text muted
slot background
slot hover
danger
success
padding small
padding medium
padding large
border radius small
border radius medium
```

Built-in UI may define styles in C++.

Plugin/server-authored UI may reference approved style tokens but must not define arbitrary renderer state.

---

# Built-In Widgets

Initial built-in widgets:

* panel
* text
* image
* button
* checkbox
* slider
* text input
* scroll area
* vertical stack
* horizontal stack
* grid
* tooltip
* item slot
* hotbar
* modal dialog

Game-specific widgets:

* item slot
* item icon
* block icon
* inventory grid
* hotbar
* durability bar
* stack count label
* recipe slot
* container title
* chat line
* player list row

---

# Inventory and Drag/Drop

Inventory UI is a first-class use case.

The system must support:

* item slot hover
* item slot click
* drag item stack
* split stack interaction
* swap stack interaction
* drop outside inventory
* tooltip on hover
* hotbar selection
* server-authoritative confirmation in multiplayer

Drag state must be explicit.

Drag payloads must be semantic, not raw pointers.

Payloads may reference:

* inventory ID
* slot index
* item stack preview
* source screen
* source element

---

# Plugin and Server UI

Plugin/server UI is declarative and constrained.

It describes:

* screen structure
* layout
* allowed widgets
* text
* item/icon references
* style token references
* semantic actions

It does not execute client code.

Plugin/server UI may not:

* define shaders
* inject rendering commands
* run scripts
* access client memory
* read raw key state
* globally capture input
* override protected system shortcuts
* trap Escape permanently
* create invisible click blockers without permission
* spoof system UI
* mutate client state directly
* reference arbitrary local files

---

## UI Document Model

A plugin/server UI document represents one screen.

A document contains:

```text
version
screen_id
owner
screen properties
root element
```

Required document fields:

```text
version
  Integer schema version.

screen_id
  Unique active screen identifier.

owner
  Source identifier.

properties
  Screen behavior flags.

root
  Root UI element.
```

Screen properties include:

```text
modal
closable
blocks_gameplay
input_mode
title
trusted
```

The client validates all fields before building the UI tree.

---

## Element Model

Every plugin/server element uses the same conceptual shape:

```text
type
id
layout
style
props
children
```

### `type`

String enum of allowed element types.

Unknown type: reject document.

### `id`

Unique within the document.

Required for:

* interactive elements
* focusable elements
* elements receiving updates
* elements emitting actions

### `layout`

Uses the approved layout primitives.

### `style`

References approved style tokens only.

### `props`

Type-specific properties.

### `children`

Ordered child list.

Only valid for container-capable elements.

---

# Allowed Plugin/Server Element Types

The following are the initial allowed element types.

No other plugin/server element types are valid unless the schema is updated.

---

## `root`

Top-level document element.

Purpose:

* owns screen content
* defines top-level layout
* may contain one child or many children depending on layout

Allowed children:

* panel
* stack_vertical
* stack_horizontal
* grid
* scroll_area
* overlay
* text
* image
* button
* item_slot
* spacer

Allowed props:

```text
none initially
```

---

## `panel`

Generic visual/container element.

Allowed children:

* panel
* stack_vertical
* stack_horizontal
* grid
* scroll_area
* overlay
* text
* image
* button
* item_slot
* spacer
* separator

Allowed props:

```text
title
  Optional display title.

clip_children
  Boolean.

hit_test
  Boolean. Defaults to true if background is interactive, false otherwise.
```

---

## `stack_vertical`

Vertical layout container.

Allowed children:

* panel
* stack_vertical
* stack_horizontal
* grid
* scroll_area
* text
* image
* button
* item_slot
* spacer
* separator

Allowed props:

```text
spacing
  Style spacing token or numeric logical units if permitted by schema.

align_x
  left | center | right | stretch

reverse
  Boolean. Defaults false.
```

---

## `stack_horizontal`

Horizontal layout container.

Allowed children:

* panel
* stack_vertical
* stack_horizontal
* grid
* scroll_area
* text
* image
* button
* item_slot
* spacer
* separator

Allowed props:

```text
spacing
  Style spacing token or numeric logical units if permitted by schema.

align_y
  top | center | bottom | stretch

reverse
  Boolean. Defaults false.
```

---

## `grid`

Grid layout container.

Allowed children:

* panel
* text
* image
* button
* item_slot
* spacer

Allowed props:

```text
columns
  Positive integer.

spacing_x
  Style spacing token or numeric logical units if permitted by schema.

spacing_y
  Style spacing token or numeric logical units if permitted by schema.

cell_width
  Optional fixed logical size.

cell_height
  Optional fixed logical size.
```

Initial grid behavior should use row-major placement.

---

## `scroll_area`

Scrollable viewport.

Allowed children:

* panel
* stack_vertical
* stack_horizontal
* grid
* text

Allowed props:

```text
scroll_x
  Boolean. Defaults false.

scroll_y
  Boolean. Defaults true.

show_scrollbar
  Boolean. Defaults true.

initial_scroll_x
  Optional number.

initial_scroll_y
  Optional number.
```

Scroll state is owned by the client UI runtime.

---

## `overlay`

Container for elements rendered above normal layout flow.

Allowed children:

* panel
* text
* image
* button
* tooltip
* spacer

Allowed props:

```text
pass_through
  Boolean. If true, overlay does not block hit testing except for interactive children.
```

---

## `text`

Text display element.

Allowed children:

```text
none
```

Allowed props:

```text
text
  UTF-8 string.

wrap
  Boolean.

align
  left | center | right

max_lines
  Optional positive integer.

overflow
  clip | ellipsis

selectable
  Boolean. Defaults false.
```

Text length must be limited by document validation.

---

## `image`

Image/icon display element.

Allowed children:

```text
none
```

Allowed props:

```text
asset
  Approved asset identifier.

tint
  Optional approved style color token.

fit
  contain | cover | stretch | original

alt
  Optional text description.
```

Server/plugin UI may only reference assets available through approved asset rules.

Missing assets render as safe placeholders.

---

## `button`

Clickable button.

Allowed children:

* text
* image
* stack_horizontal
* stack_vertical

Allowed props:

```text
label
  Optional UTF-8 string.

action
  Plugin/server-defined action identifier.

enabled
  Boolean. Defaults true.

focusable
  Boolean. Defaults true.

tooltip
  Optional UTF-8 string.

confirm
  Optional confirmation behavior token.
```

Buttons emit semantic click actions.

---

## `checkbox`

Boolean toggle.

Allowed children:

* text

Allowed props:

```text
label
  Optional UTF-8 string.

checked
  Boolean.

action
  Action identifier emitted on change.

enabled
  Boolean. Defaults true.

focusable
  Boolean. Defaults true.
```

Server remains authoritative for gameplay-affecting checkbox changes.

---

## `slider`

Numeric value control.

Allowed children:

```text
none
```

Allowed props:

```text
min
  Number.

max
  Number.

value
  Number.

step
  Optional number.

action
  Action identifier emitted on change or commit.

enabled
  Boolean. Defaults true.

focusable
  Boolean. Defaults true.
```

Sliders are allowed for settings-like/plugin menus but should be rate-limited if server-owned.

---

## `text_input`

Text entry field.

Allowed children:

```text
none
```

Allowed props:

```text
value
  UTF-8 string.

placeholder
  Optional UTF-8 string.

max_length
  Positive integer within client limit.

submit_action
  Optional action identifier.

change_action
  Optional action identifier.

multiline
  Boolean. Defaults false.

enabled
  Boolean. Defaults true.

focusable
  Boolean. Defaults true.
```

Text input must use text input events, not raw keycodes.

Server/plugin text fields must be length-limited.

---

## `item_slot`

Game item display/interact element.

Allowed children:

```text
none
```

Allowed props:

```text
item
  Optional item identifier.

count
  Optional integer.

durability
  Optional normalized value.

slot_index
  Optional integer.

inventory_id
  Optional inventory identifier.

action
  Optional action identifier.

enabled
  Boolean. Defaults true.

focusable
  Boolean. Defaults true.

tooltip
  Optional tooltip mode or text.
```

Item slots must emit semantic actions, not raw inventory mutations.

Examples of semantic interaction:

```text
click
right_click
drag_start
drag_end
hover
```

The server validates all gameplay-affecting item slot actions.

---

## `spacer`

Non-interactive layout spacing element.

Allowed children:

```text
none
```

Allowed props:

```text
size
  Logical size or style spacing token.

axis
  horizontal | vertical | both

fill
  Boolean. If true, consumes available space where supported.
```

---

## `separator`

Non-interactive visual divider.

Allowed children:

```text
none
```

Allowed props:

```text
axis
  horizontal | vertical

thickness
  Optional style size token.

style
  Optional separator style token.
```

---

## `tooltip`

Tooltip content element.

Allowed children:

* text
* image
* stack_vertical
* stack_horizontal
* item_slot

Allowed props:

```text
delay
  Optional duration token.

placement
  cursor | above | below | left | right

follow_cursor
  Boolean.
```

Tooltips should not receive normal interaction by default.

---

## `progress_bar`

Displays progress.

Allowed children:

```text
none
```

Allowed props:

```text
value
  Number.

min
  Number. Defaults 0.

max
  Number. Defaults 1.

label
  Optional UTF-8 string.

variant
  normal | success | warning | danger
```

Progress bars are display-only unless a future schema revision adds interaction.

---

# Common Element Fields

All elements may support:

```text
id
type
visible
enabled
layout
style
children
```

Common layout fields:

```text
width
height
min_width
min_height
max_width
max_height
sizing_x
sizing_y
margin
padding
anchor
offset
clip
```

Common style fields:

```text
background
border
text_color
padding
spacing
corner_radius
opacity
```

Style fields reference approved tokens.

Raw colors should be avoided in the initial schema.

---

# Actions

UI emits semantic actions.

Client-defined interaction types:

```text
click
right_click
hover
drag_start
drag_end
value_change
text_submit
focus
blur
close
```

Plugin/server-defined action IDs identify intent.

Example conceptual action payload:

```text
screen_id
element_id
interaction_type
action_id
optional value payload
```

The client decides when actions fire.

The plugin/server only names the action.

---

## Server UI Action Flow

```text
Server/plugin sends UI document
    ↓
Client validates document
    ↓
Client builds retained UI tree
    ↓
Player interacts
    ↓
Client emits semantic UI action
    ↓
Server validates action
    ↓
Server applies gameplay result
    ↓
Server sends updated UI/world state
```

The server must validate:

* screen is still open
* element exists
* action is allowed
* player is allowed to perform it
* gameplay state still permits the result

---

# Document Updates

Initial implementation may resend full documents.

Future implementation may support patches.

Patch operations may include:

```text
replace element
update props
update style
insert child
remove child
close screen
```

Patch support must preserve validation rules.

---

# Validation Rules

Client validates all plugin/server UI documents.

Validation includes:

* supported schema version
* known element types
* valid root element
* unique IDs
* valid ID format
* valid child relationships
* valid layout fields
* valid style tokens
* valid action identifiers
* valid asset identifiers
* valid UTF-8
* max text length
* max tree depth
* max element count
* max children per element
* max active screens
* max update frequency

Malformed documents fail safely.

---

# Suggested Limits

Exact values should be configurable, but the system should define limits for:

```text
Max elements per screen
Max tree depth
Max children per element
Max text length per element
Max total text length per screen
Max active plugin screens
Max active server screens
Max UI updates per second
Max image/icon references per screen
Max text input length
```

The client may reject, truncate, or degrade documents that exceed limits.

---

# Screen Ownership

Screens have ownership:

```text
Client
Server
Plugin
System
```

Ownership affects:

* close behavior
* allowed widgets
* allowed style tokens
* trusted status
* action routing
* validation strictness

Examples:

```text
Client
  pause menu, settings, local HUD

Server
  container screen, shop, NPC dialog

Plugin
  custom gameplay menu

System
  disconnect screen, fatal error, keybind capture
```

Server/plugin screens are not trusted as system UI.

---

# Security and Trust

The UI system is part of the multiplayer trust boundary.

Rules:

* client validates all server/plugin UI
* server validates all gameplay-affecting UI actions
* server/plugin UI is declarative, not executable
* server/plugin UI cannot impersonate system UI
* protected UI regions may be reserved for system/client UI
* dangerous actions require confirmation
* malformed UI documents must fail safely
* text must be sanitized for rendering
* assets must resolve through approved asset rules

---

# Data Binding

Built-in UI may bind to local client view models.

Plugin/server UI should initially use explicit document state and document updates.

Avoid arbitrary client-side bindings in the first schema version.

Future limited bindings may be added for safe concepts such as:

```text
player name
server name
item preview
read-only stat values
localized string keys
```

---

# UI Updates and Invalidation

The UI system should avoid unnecessary work.

Dirty states may include:

```text
layout dirty
style dirty
text dirty
paint dirty
visibility dirty
hit-test dirty
```

Layout updates when:

* element bounds change
* children change
* text changes
* size-affecting style changes
* viewport changes
* UI scale changes

Paint commands update when:

* layout changes
* visual style changes
* widget state changes
* text/image content changes

---

# Layering

UI uses explicit layers:

```text
HUD
Screen
Popup
Tooltip
Modal
CursorDragPreview
SystemOverlay
DebugOverlay
```

This avoids fragile ordering based only on tree traversal.

---

# Cursor Behavior

The UI system coordinates cursor state with input mode.

Examples:

```text
Gameplay
  cursor hidden/locked

Inventory
  cursor visible/unlocked

Pause Menu
  cursor visible/unlocked

Text Entry
  cursor visible, keyboard focus active

Debug UI
  cursor visible when ImGui captures mouse
```

---

# Navigation Support

Focusable widgets declare navigation behavior.

The system should support:

* tab order
* directional navigation
* default focused element
* activation action
* cancel action

Navigation focus is separate from keyboard text focus.

---

# Accessibility Considerations

The initial system does not need full accessibility support, but should avoid blocking it later.

Design considerations:

* scalable UI
* keyboard navigation
* readable text sizes
* semantic widget types
* explicit labels
* style tokens instead of hardcoded colors

---

# Error Handling

Invalid UI operations fail safely.

Examples:

```text
Unknown element type
  Reject document.

Invalid style token
  Use fallback or reject.

Missing item icon
  Use missing icon.

Oversized document
  Reject or truncate.

Invalid action
  Ignore and log.

Protected widget requested by plugin
  Reject.
```

Debug builds should expose detailed diagnostics.

Release builds should avoid crashing from malformed UI data.

---

# Threading Rules

Live UI tree mutation occurs on the client/main thread.

Worker threads may prepare:

* text layout cache data
* image decode results
* icon atlas updates
* serialized UI documents
* validation results

Worker threads must not mutate the live UI tree directly.

Renderer upload/submission follows renderer thread rules defined elsewhere.

---

# Relationship to Gameplay

UI communicates with gameplay through actions, commands, or view models.

UI must not directly own:

* world chunks
* entities
* registries
* physics state
* networking sockets
* save data

Good:

```text
UI emits semantic action:
  request_use_inventory_slot
```

Bad:

```text
UI directly mutates inventory storage.
```

---

# Relationship to Networking

Networked UI must be server-authoritative when it affects gameplay.

The client may predict visual-only feedback, but gameplay results require server confirmation.

Example:

```text
Client clicks shop buy button
  client may play click sound
  server validates purchase
  server updates inventory/currency
  client updates UI from server response
```

---

# Relationship to Assets

UI references engine assets by stable asset names.

Examples:

```text
arvane:ui/panel_default
arvane:items/stone
arvane:icons/warning
```

Plugin/server UI may only reference assets available through approved asset loading rules.

Missing assets use safe placeholders.

---

# Implementation Phasing

## Phase 1: Core UI Runtime

* retained UI tree
* stable element IDs
* layout primitives
* paint commands
* mouse hit testing
* focus state
* basic widgets
* UI renderer integration

## Phase 2: Game UI

* HUD
* hotbar
* inventory grid
* item slots
* tooltips
* pause menu
* settings skeleton
* chat input

## Phase 3: Plugin/Server UI

* declarative UI document schema
* document validation
* server-sent screen creation
* semantic UI actions
* plugin menu support
* document limits

## Phase 4: Polish

* animations
* theme improvements
* controller navigation
* richer text
* virtualized lists
* accessibility improvements

---

# Design Invariants

* Dear ImGui is for development UI only.
* User-facing UI is retained.
* Immediate-style APIs are authoring helpers, not the runtime model.
* Layout is deterministic and independent of rendering.
* Widgets emit semantic actions.
* Gameplay input only receives unconsumed input.
* Focus state is explicit.
* Mouse capture is explicit.
* Modal UI uses a stack.
* Plugin/server UI is declarative, not executable.
* Server-sent UI is validated before display.
* Server validates gameplay-affecting UI actions.
* UI does not directly mutate world state.
* Widgets do not directly call renderer APIs.
* UI tree mutation happens on the client/main thread.

---