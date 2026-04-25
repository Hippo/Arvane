# Inventory and Interaction

## Purpose

This document defines Arvane’s baseline **inventory, item, and interaction model**.

It covers:

- inventory structure
- stack rules
- item definitions
- held item behavior
- block placing
- block breaking
- interaction raycasts
- item pickup and dropping
- interaction priority rules

This document exists because a voxel game should not treat interaction as only “break block” and “place block.”

The player should interact with the world through a consistent item and inventory model.

---

## Design Goals

The inventory and interaction system should be:

- explicit
- server-authoritative
- data-driven where practical
- compatible with block/item registries
- predictable for gameplay
- easy to extend with new items, blocks, and entities

The interaction system must avoid hard-coded behavior scattered across the player controller, world, physics, and item systems.

---

## Scope

This document defines the gameplay rules for inventory and interaction.

This document does not define:

- UI layout
- crafting
- recipes
- persistence file formats
- networking packet formats
- block registry internals
- physics collision internals

Those belong in separate documents.

---

## Core Concepts

The system is built from the following concepts:

- **Item definition**
  - static authored item type

- **Item stack**
  - runtime quantity of one item type with optional stack data

- **Inventory**
  - ordered collection of item slots

- **Held item**
  - item stack in the currently selected hotbar slot

- **Interaction context**
  - player, held item, target, action type, and world state

- **Interaction result**
  - accepted action, rejected action, consumed item, modified world, etc.

---

# Item Model

## Item Definitions

Every item type must be registered through the item registry.

An item definition should include:

- item ID
- stable authored name
- display name
- max stack size
- optional associated block state for placeable block items
- item category
- use behavior
- primary behavior
- secondary behavior
- durability behavior, if applicable later
- metadata/schema rules, if applicable later

Example authored names:

- `arvane:stone`
- `arvane:dirt`
- `arvane:grass_block`
- `arvane:wood_pickaxe`
- `arvane:torch`

Runtime item IDs must not be hard-coded into gameplay logic.

Gameplay should reference item definitions by registry handles or authored names where appropriate.

---

## Item ID vs Item Stack

An item ID identifies the type of item.

An item stack represents a runtime quantity of that item.

Example:

```cpp
struct ItemStack {
  ItemID item;
  u16 count;
  Optional<ItemStackData> data;
};
```

The exact implementation may differ, but the conceptual split must remain.

---

## Empty Item Stack

Empty inventory slots must have an explicit representation.

Allowed representations:

* nullable/optional stack
* stack with `count == 0`
* dedicated empty stack type

The chosen representation must be consistent across inventory, UI, networking, and persistence.

Do not represent empty slots as an arbitrary item ID.

---

## Stack Data

Some items may eventually need per-stack data.

Examples:

* durability
* custom name
* stored block state
* tool modifiers
* container contents
* enchantments or upgrades

The baseline system does not need all of these immediately, but the item stack model should not prevent them.

Stack data must be considered when merging stacks.

Two stacks can only merge if they are compatible.

---

# Inventory Model

## Inventory Structure

A player inventory is an ordered collection of slots.

At minimum, the player should have:

* hotbar slots
* main inventory slots

The hotbar is a subset of the player inventory and is directly selectable by the player controller.

Recommended baseline:

* hotbar: 9 slots
* main inventory: gameplay-defined fixed size

The exact slot counts must be centralized constants/config values.

---

## Inventory Slot

Each inventory slot contains either:

* an empty slot
* one item stack

A slot must not contain multiple unrelated item stacks.

---

## Inventory Ownership

Inventories are owned by gameplay entities or world objects.

Examples:

* player inventory
* dropped item entity stack
* chest inventory
* machine inventory
* NPC inventory, if added later

The inventory system should be reusable and not hard-coded only for players.

---

## Inventory Operations

The inventory system should expose clear operations:

* get slot
* set slot
* clear slot
* insert stack
* remove amount
* split stack
* merge stacks
* swap slots
* move stack between inventories
* find first compatible stack
* find first empty slot

Inventory operations should return explicit results instead of silently failing.

Example result kinds:

* success
* partial success
* rejected: incompatible item
* rejected: full inventory
* rejected: invalid slot
* rejected: insufficient quantity

---

## Stack Rules

### Max Stack Size

Every item definition defines a max stack size.

Examples:

* blocks may stack to a configured block stack size
* tools may stack to 1
* special items may have custom limits

The max stack size must be read from the item definition.

Do not hard-code stack size in inventory operations.

### Stack Merging

Stacks may merge if:

* they have the same item ID
* their stack data is compatible
* the target stack is below max stack size

Merge operations may be partial.

### Stack Splitting

Stacks may be split into smaller stacks.

Splitting must preserve item type and stack data.

### Stack Overflow

Insertion into an inventory may leave a remainder.

Inventory insert operations should return the leftover stack if not all items fit.

---

# Held Item Semantics

## Selected Hotbar Slot

The selected hotbar slot is part of player state.

The held item is the stack currently in that slot.

If the selected slot is empty, the held item is empty.

---

## Held Item Use

Held item behavior is resolved through interaction logic.

The player controller does not directly modify the world or inventory.

The controller provides:

* selected slot
* primary/secondary action
* target information
* input state

Gameplay resolves:

* whether the held item can act
* whether the action is valid
* whether the item is consumed
* whether the world changes

---

## Consuming Held Items

When an item use consumes an item:

* the stack count decreases
* if count reaches zero, the slot becomes empty
* the server-authoritative state is the source of truth

Examples:

* placing a block consumes one block item
* using a consumable consumes one item
* using a tool may reduce durability instead of count

---

# Interaction Model

## Interaction Context

Every interaction should be resolved from an explicit context.

A baseline interaction context should include:

* player entity
* world reference
* selected hotbar slot
* held item stack
* action type
* target result
* player eye position
* player look direction
* reach distance
* tick/time information if needed

The context should not require direct access to raw input devices.

---

## Interaction Actions

Baseline action types:

* primary press
* primary hold
* primary release
* secondary press
* secondary hold
* secondary release
* pick block
* drop item

Primary action usually means:

* break block
* attack entity
* use tool primary behavior

Secondary action usually means:

* use item
* place block
* interact with block
* interact with entity

---

## Interaction Result

Interaction resolution should return an explicit result.

Example result fields:

* accepted/rejected
* reason for rejection
* world mutation requested/applied
* inventory mutation requested/applied
* animation/sound event
* cooldown applied
* whether client prediction is allowed

This keeps interaction behavior debuggable and network-friendly.

---

# Interaction Raycasts

## Source

Interaction raycasts originate from the player eye position.

The ray direction comes from the player camera forward direction.

## Reach

Raycasts use the configured reach distance.

Reach distance must be validated server-side.

The client may compute raycasts for responsiveness, but the server must verify:

* origin is plausible
* direction is plausible
* target is within reach
* target is interactable
* action is legal

## Hit Types

The interaction raycast should support:

* no hit
* block hit
* entity hit

Block hit result should include:

* block position
* block state
* hit face
* hit point
* hit distance

Entity hit result should include:

* entity ID
* hit point
* hit distance

## Nearest Valid Target

When multiple targets exist, the nearest valid target along the ray should be selected.

Target filtering may depend on the action.

Examples:

* attack may prefer entities
* placement may require a block face
* block interaction may require an interactable block

---

# Interaction Priority

Interaction priority must be stable and explicit.

## Primary Interaction Priority

Recommended primary interaction order:

1. targeted entity primary action
2. held item primary action on target
3. block breaking
4. no-op

Examples:

* attack entity
* use tool against targeted block
* begin breaking block

## Secondary Interaction Priority

Recommended secondary interaction order:

1. targeted entity secondary interaction
2. held item secondary use on target
3. targeted block interaction
4. place held block item
5. held item secondary use with no target
6. no-op

This gives entities and held items first chance to consume contextual use, while still allowing normal block placement.

## Sneak Modifier

Sneaking may alter interaction priority.

Baseline rule:

* sneaking allows bypassing block interaction in favor of item placement/use when appropriate

Example:

* right-clicking a chest normally opens it
* sneak-right-clicking with a block may place against the chest instead

This behavior must be explicit and not left to individual block implementations to invent inconsistently.

---

# Block Breaking

## Starting a Break

Block breaking begins from a valid primary interaction on a block target.

The break attempt should track:

* target block position
* target block state
* player entity
* held item
* accumulated progress
* required break time

## Break Progress

Breaking may be instant or timed depending on block and tool rules.

Baseline behavior:

* primary press starts break attempt
* primary hold continues break progress
* primary release cancels or pauses based on gameplay policy
* changing target resets or revalidates progress
* block changes invalidate the break attempt

## Break Time

Break time is determined by gameplay rules.

Inputs may include:

* block definition
* block state
* held item
* tool category
* tool strength
* player modifiers

The constants and formulas must be centralized.

Do not hard-code break speed logic inside the player controller.

## Successful Break

When a block is successfully broken:

* world block is changed, usually to air
* drops are generated according to block/item rules
* held item durability may be reduced
* lighting/meshing/world updates are triggered
* inventory pickup may occur if drops are directly collected

The exact drop model may evolve, but the interaction system must emit the necessary world mutation.

## Failed Break

Breaking should fail if:

* target is out of reach
* target no longer matches
* block is unbreakable
* player lacks permission
* world rules reject mutation

Failure must be explicit and debuggable.

---

# Block Placement

## Placeable Items

An item may define an associated block placement behavior.

Common case:

* item stack `arvane:stone` places block state `arvane:stone`

More complex cases:

* stairs choose orientation from player facing
* slabs choose half from hit position
* torches choose mounted orientation from hit face
* doors place multiple blocks

Placement behavior belongs to item/block gameplay definitions, not the player controller.

---

## Placement Target

Block placement usually uses a block hit result.

The placed position is derived from:

* targeted block position
* hit face normal

Example:

* targeting the top face of `(10, 64, 10)` places at `(10, 65, 10)`

## Placement Validation

Placement must validate:

* target position is writable
* target block can be replaced
* placed block shape does not collide with player or blocked entities
* support rules are satisfied, if any
* world permissions allow the mutation
* chunk/world area is loaded and editable

## Replaceable Blocks

Some blocks may be replaceable.

Examples:

* tall grass
* fluids
* snow layers, depending on rules

If the targeted block is replaceable, placement may occur in the targeted block position rather than the adjacent face position.

This must be defined by block state behavior.

## Orientation Rules

Some placed blocks derive state from context.

Examples:

* player horizontal facing
* clicked face
* hit position within block
* sneaking state
* adjacent blocks
* water/fluid state, if added later

Placement must produce a concrete block state ID before mutation.

---

# Block Interaction

Blocks may define secondary interaction behavior.

Examples:

* open door
* toggle lever
* open container
* activate machine
* harvest crop
* edit sign

Block interactions receive the interaction context and may accept or reject the action.

A block interaction may:

* mutate block state
* open UI
* interact with inventory
* emit sound/event
* consume held item
* do nothing

Block behavior must be registry-driven or dispatched through clear gameplay interfaces.

---

# Entity Interaction

The interaction system should allow entities to participate.

Examples:

* attack entity
* talk to NPC
* mount entity
* pick up loose item
* interact with dropped object

Entity interaction should use the same interaction context/result model where practical.

---

# Pickup Rules

## Dropped Item Entity

Items dropped into the world should be represented as entities containing item stacks.

A dropped item entity should include:

* item stack
* world position
* velocity
* pickup delay
* owner/source metadata if needed
* age/despawn timer if used

## Automatic Pickup

Players may automatically pick up nearby dropped items.

Baseline rules:

* item must be within pickup radius
* pickup delay must have expired
* inventory must accept at least part of the stack
* picked-up amount is inserted into inventory
* leftover amount remains in world if inventory is full

Pickup radius and delay must be centralized constants.

## Manual Pickup

Manual pickup may be added later, but automatic pickup is the baseline voxel-game behavior.

---

# Drop Rules

## Dropping From Inventory

Players may drop items from inventory or hotbar.

Drop operation should specify:

* source slot
* amount
* drop position
* initial velocity
* pickup delay

## Dropping Held Item

Baseline controls may support:

* drop one item from selected slot
* drop full stack from selected slot

Exact keybinds belong to input configuration.

## World Drop Creation

When an item is dropped:

* inventory stack is reduced
* dropped item entity is spawned
* spawned item receives initial velocity
* pickup delay prevents immediate re-pickup

The operation must be server-authoritative.

---

# Inventory and World Mutation

Inventory and world changes should be resolved in a controlled order.

For example, block placement should generally:

1. validate held stack
2. validate placement target
3. validate collision/permission
4. mutate world
5. consume item
6. trigger lighting/mesh/update events

If mutation fails at any validation step, the held item must not be consumed.

For server-authoritative gameplay, world and inventory mutation should happen as one logical transaction.

---

# Client Prediction

The client may predict some interactions for responsiveness.

Good candidates:

* block highlight
* swing animation
* local placement preview
* break progress visual
* hotbar selection

Server-authoritative actions:

* actual block placement
* actual block breaking
* item consumption
* item pickup
* item drop
* inventory mutation

If client prediction disagrees with the server, the client must reconcile to server state.

---

# Permissions and Validation

Every interaction that mutates world or inventory must be validated.

Validation includes:

* player exists
* player is alive/active if applicable
* selected slot is valid
* held item exists if required
* target is within reach
* target world position is loaded
* target is editable
* action is allowed by game mode
* inventory mutation is legal
* block/entity behavior accepts the action

The client must never be trusted as the source of truth for world mutation.

---

# Constants and Configuration

All gameplay constants must be centralized.

Examples:

* hotbar slot count
* player inventory slot count
* default reach distance
* item pickup radius
* item pickup delay
* item despawn time
* default block stack size
* default drop velocity
* break speed multipliers
* creative/admin reach distance, if used

Do not scatter magic numbers throughout item, inventory, or interaction code.

---

# Debugging and Tooling

Useful debug information includes:

* selected hotbar slot
* held item stack
* current interaction target
* target distance
* current break target
* break progress
* last interaction result
* inventory insert failure reason
* placement validation failure reason
* pickup/drop entity count nearby

Interaction failures should be easy to inspect.

A failed placement should be able to explain why it failed.

---

# Required Invariants

Implementations must preserve these invariants:

* item definitions come from the registry
* item stacks are distinct from item definitions
* inventory slots hold at most one stack
* stack merging respects item ID, stack data, and max stack size
* selected hotbar slot is authoritative player state
* held item behavior is resolved by gameplay systems, not raw input code
* interaction raycasts use eye position, look direction, and configured reach
* server validates all mutating interactions
* placement never consumes an item unless the world mutation succeeds
* block breaking is invalidated when the target changes unexpectedly
* pickup/drop behavior mutates inventory and world consistently
* all constants are centralized

---

# Minimum Baseline Behavior

A valid first implementation must support:

* registered item definitions
* item stacks with counts
* player inventory with hotbar
* hotbar selection
* held item lookup
* stack insertion/removal/merge/split
* block items that place blocks
* primary block breaking intent
* secondary block placement intent
* interaction raycast result
* dropped item entities
* automatic pickup
* dropping items from inventory
* server-authoritative validation path

This baseline is enough to move Arvane beyond a rendering demo and into a real gameplay loop.
