# Command System

## Purpose

This document defines the Arvane command system.

The command system provides a structured way to execute engine, game, server, and client actions from supported command sources.

Commands are used for:

* player-issued commands
* dedicated server console commands
* client-local utility commands
* server administration
* plugin-defined behavior
* test and tooling entry points

The command system is not tied to chat rendering, debug UI, or a specific console implementation. Those systems are input frontends. The command system is the execution framework.

---

## Core Goals

The command system must be:

* domain-bound
* permission-aware
* server-authoritative where required
* ergonomic to register and implement
* extensible by plugins
* type-safe where practical
* testable without a full game runtime
* independent from any specific UI frontend

Commands should be implemented as small functions that receive a complete execution context.

---

## Command Domains

Every command belongs to a command domain.

The domain determines where the command may execute.

```cpp
enum class CommandDomain {
    ClientBound,
    ServerBound,
};
```

---

## ClientBound Commands

A `ClientBound` command executes entirely on the local client.

Client-bound commands:

* do not contact the server
* do not require a server permission check
* must not mutate authoritative server or world state
* may inspect or modify local client-only systems

Examples of client-bound behavior include:

* reloading local textures
* reloading local shaders
* printing local debug information
* toggling local debug overlays
* dumping local renderer state

A client-bound command may still validate its arguments and return structured errors.

Client-bound commands are not a security boundary. They only affect the local process.

---

## ServerBound Commands

A `ServerBound` command executes on the server.

Server-bound commands:

* are sent to the server when invoked by a player
* are executed directly by the server when invoked from the server console
* are checked against the permission system before execution
* may mutate authoritative state
* return a structured command result to the sender

Most gameplay, administration, world, entity, inventory, and player-management commands should be server-bound.

The client must not execute server-bound commands locally.

---

## Command Sources

A command source identifies where the command came from.

Initial command sources:

```cpp
enum class CommandSourceKind {
    Player,
    ServerConsole,
};
```

Other sources may be added later if needed.

Examples of possible future sources:

* script
* remote console
* internal system
* automated test harness

The debug window does not need a command box. Debug UI actions may call direct engine APIs or submit commands internally later if that becomes useful.

---

## Player Command Source

A player source represents a command submitted by an online player.

A player source should contain:

* player profile ID
* optional online player/session handle
* player display name
* current world, if available
* current entity, if available
* current position, if available

The profile ID is the stable authority identity.

Player names are convenience labels and must not be treated as stable identity.

---

## Server Console Command Source

A server console source represents a command submitted by the dedicated server console or integrated server console.

A server console source:

* is not associated with a player entity
* has no implicit world position
* has no implicit relative-coordinate anchor unless one is explicitly supplied
* is treated as a privileged server-side source

Server console commands may still fail when they require a player-specific context that was not supplied.

Example:

```text
/tp ~ ~5 ~
```

This is invalid from the server console unless the command provides an explicit target or anchor.

---

## Command Identity

Each command has a stable command name.

Command names should be lowercase.

Recommended command-name format:

```text
namespace.path.action
```

or a simple command root where appropriate:

```text
time
world.time
player.teleport
client.reload_textures
```

Command names are internal registry keys. User-facing aliases may be added separately.

Command names should not contain whitespace.

---

## Command Permission Nodes

Server-bound commands may declare an optional custom permission node.

Example:

```text
arvane.command.teleport
arvane.command.spawn
arvane.command.setspawn
arvane.command.world.time
plugin.example.command.spawn_arena
```

Command permissions use the existing custom permission-node system.

The permission system already supports:

* dot-separated custom permission nodes
* wildcard matching
* exact-match priority
* more-specific wildcard priority
* `ALLOW`
* `DENY`
* `UNSET`
* DENY precedence

The command dispatcher must respect the resolved permission state.

If a command has no explicit permission node, the command must define a clear default policy.

Recommended default policy:

* `ClientBound`: no permission node required
* `ServerBound` from `Player`: permission required unless explicitly marked public
* `ServerBound` from `ServerConsole`: allowed unless the command explicitly disallows console execution

---

## Command Registration

Commands are registered into a command registry.

The registry is the source of truth for native command metadata and execution behavior.

Native command definitions should be registered in code, not duplicated in data files.

```cpp
struct Command {
    std::string name;
    CommandDomain domain;

    std::string permission;

    std::string description;
    std::string usage;

    CommandExecuteFn execute;
};
```

An empty string for `permission` represents no permission required.

The exact string-owning types may be adjusted to match Arvane conventions.

The command registry should support:

* registering commands
* rejecting duplicate command names
* resolving commands by name
* enumerating commands for help output
* optional alias registration
* optional command replacement rules for plugins, if explicitly allowed

Duplicate command registration is an error unless an explicit override mechanism exists.

---

## Command Execution Function

A command executes through a single function.

```cpp
using CommandExecuteFn = arv::FunctionRef<CommandResult(CommandCtx&)>;
```

or, if commands need to own callable state:

```cpp
using CommandExecuteFn = std::function<CommandResult(CommandCtx&)>;
```

For hot-path and allocation-sensitive code, prefer an Arvane-owned callable policy.

Commands should not directly print to stdout, write chat messages, or send packets.

Commands return `CommandResult`.

---

## Command Context

A command receives a `CommandCtx`.

```cpp
struct CommandCtx {
    Command const& command;
    CommandSender const& sender;
    CommandArgs args;
    CommandServices& services;
};
```

The context is the only object a command handler should need for normal command execution.

The context should provide convenience methods for argument parsing, sender inspection, permission checks, and result creation.

---

## Command Sender

A command sender describes who or what submitted the command.

```cpp
struct CommandSender {
    CommandSourceKind kind;

    std::optional<arv::crypto::PlayerProfileID> player_profile_id;
    std::optional<PlayerSessionId> player_session_id;
    std::optional<EntityId> entity_id;
    std::optional<WorldId> world_id;

    std::string_view display_name;
};
```

The exact handle types should match the rest of the engine.

Rules:

* `PlayerProfileID` is the stable player identity.
* Online player handles are only valid while the player is connected.
* Display names are not stable identifiers.
* Server console senders do not have an implicit player profile ID.
* Server console senders do not have an implicit entity or position.

---

## Command Services

`CommandServices` provides access to systems needed by command handlers.

It should be a narrow service bundle, not a global service locator.

Example:

```cpp
struct CommandServices {
    PermissionService* permissions;
    PlayerService* players;
    WorldService* worlds;
    RegistryService* registries;
    NetworkCommandService* network;
    ClientCommandService* client;
};
```

The exact services available may differ between client-bound and server-bound execution.

Rules:

* Client-bound execution receives client-local services.
* Server-bound execution receives server-authoritative services.
* Commands must not reach into unrelated globals.
* Missing services should produce a clean command error, not a crash.

---

## Command Arguments

Raw command arguments may initially be stored as string views.

```cpp
struct CommandArgs {
    std::vector<std::string_view> tokens;
};
```

The command parser owns or references the original input buffer for the duration of execution.

`std::string_view` tokens must not outlive the command input storage.

Argument parsing should be done through typed readers, not manually in every command.

---

## Argument Reader

`CommandCtx` should expose argument parsing helpers.

Example:

```cpp
auto target = ctx.arg<OnlinePlayer>(0);
auto amount = ctx.arg<int>(1);
auto id = ctx.arg<Identifier>(2);
```

or:

```cpp
CommandArgReader args = ctx.args();

auto target = args.read<OnlinePlayer>();
auto amount = args.read<int>();
auto id = args.read<Identifier>();
```

The second form is usually more ergonomic for commands with ordered arguments.

The reader should track the current cursor and produce structured errors.

---

## Argument Validation

Argument parsing should be extensible through typed validators.

Conceptually:

```cpp
template <typename T>
struct ArgValidator {
    static CommandArgResult<T> parse(CommandArgParseCtx& ctx, std::string_view token);
};
```

Plugins should be able to register custom argument parsers for plugin-defined types.

The engine should provide built-in validators for common types.

---

## Built-In Argument Types

The command system should provide parsers for primitive and engine-level types.

Required primitive parsers:

* signed integers
* unsigned integers
* floating-point numbers
* booleans
* strings
* quoted strings, if supported by the tokenizer

Required Arvane parsers:

* `Identifier`
* `arv::crypto::PlayerProfileID`
* online player by name
* world by name
* world by UUID
* target selector
* relative coordinate
* position/vector composed from absolute and relative coordinate parts

---

## Identifier Arguments

Identifiers use the engine identifier format.

Example:

```text
arvane:stone
arvane:pig
plugin_name:custom_block
```

The identifier parser should only validate syntax.

Resolving the identifier to a block, item, entity type, or other registry object should be done by a type-specific parser or command handler using registry services.

---

## PlayerProfileID Arguments

The command argument system must support parsing `arv::crypto::PlayerProfileID`.

`PlayerProfileID` is the hashed player identity produced by the profile system.

Rules:

* `PlayerProfileID` is stable identity.
* Player names are convenience lookup keys only.
* Commands that persist player references should prefer `PlayerProfileID`.
* Commands that target online players may support name lookup for ergonomics.
* Ambiguous or missing player names must produce structured errors.

A profile ID should support the canonical display form defined by the profile system.

If profile IDs are displayed with a `0x` prefix elsewhere in the engine, command parsing should accept that canonical format.

---

## Online Player Arguments

An online-player parser resolves a currently connected player by name.

Example:

```text
SomePlayer
```

Rules:

* Online name lookup is case-insensitive only if the player system defines names as case-insensitive.
* Ambiguous partial matches should fail unless exact matching is required.
* Offline players are not returned by the online-player parser.
* Commands that can operate on offline player data should use `PlayerProfileID` or a dedicated profile lookup parser instead.

The parser should return a stable handle or profile ID plus the current session handle as needed.

---

## World Arguments

World arguments should support lookup by both name and UUID.

Examples:

```text
overworld
550e8400-e29b-41d4-a716-446655440000
```

Rules:

* UUID lookup is stable.
* Name lookup is ergonomic.
* Ambiguous world names must fail.
* Commands that persist world references should prefer UUIDs or stable world IDs.
* Display names must not be treated as the only source of identity.

---

## Target Selectors

The command argument system should support target selectors.

Example:

```text
@e[type=arvane:pig]
```

Selectors are a query language over command targets.

Initial selector support may be minimal, but the parser should be designed so selector filters can expand later.

Potential selector roots:

```text
@s
@p
@a
@e
```

Potential selector filters:

```text
type=arvane:pig
world=overworld
distance=..16
limit=1
```

Selectors should not execute during tokenization.

A selector should parse into a structured selector object.

Selector resolution should happen during command execution using command services.

Selector resolution is domain-sensitive:

* client-bound selectors can only inspect local client-visible state
* server-bound selectors resolve against authoritative server state

Selectors must respect visibility and permission rules where applicable.

---

## Relative Arguments

The argument system should support relative coordinate syntax.

Example:

```text
~
~5
~-3
```

A relative coordinate is not a number by itself. It is a coordinate expression that requires an anchor.

```cpp
struct RelativeCoord {
    bool relative;
    double value;
};
```

Examples:

```text
10     absolute 10
~      anchor + 0
~5     anchor + 5
~-2    anchor - 2
```

A position parser may read three coordinate expressions:

```cpp
struct CommandPosition {
    RelativeCoord x;
    RelativeCoord y;
    RelativeCoord z;
};
```

The position is resolved against a command anchor.

The anchor may come from:

* sender entity position
* explicit target entity
* explicit world position
* command-specific context

If no anchor exists, relative coordinates must fail.

Server console senders do not have an implicit anchor.

---

## Tokenization

The tokenizer converts command input into tokens.

Minimum behavior:

* split on ASCII whitespace
* preserve token order
* ignore leading command prefix if supplied by frontend
* report empty input cleanly

Optional behavior:

* quoted strings
* escaped quotes
* escaped whitespace

The command system should not require every frontend to include the slash prefix.

Frontends may strip `/` before dispatch.

The dispatcher should operate on normalized command text.

---

## Dispatch Pipeline

Client-bound dispatch:

```text
frontend input
  ↓
normalize command text
  ↓
client command registry lookup
  ↓
parse arguments
  ↓
execute locally
  ↓
return result to local frontend
```

Server-bound dispatch from player:

```text
client frontend input
  ↓
normalize command text
  ↓
client identifies command as ServerBound
  ↓
send command request to server
  ↓
server registry lookup
  ↓
server permission check
  ↓
server parses and validates arguments
  ↓
server executes command
  ↓
server returns command result
  ↓
client displays result
```

Server-bound dispatch from server console:

```text
server console input
  ↓
normalize command text
  ↓
server registry lookup
  ↓
permission/source validation
  ↓
parse and validate arguments
  ↓
execute command
  ↓
write result to console/log sink
```

The server must not trust the client’s command-domain classification.

The server always performs its own registry lookup, validation, and permission checks.

---

## Permission Checks

Server-bound player commands are checked through the permission system.

Command permissions are custom permission nodes.

Example:

```cpp
Command{
    .name = "player.teleport",
    .domain = CommandDomain::ServerBound,
    .permission = "arvane.command.player.teleport",
    .description = "...",
    .usage = "...",
    .execute = execute_player_teleport,
};
```

Permission check behavior:

```text
if command is ServerBound and sender is Player:
    if command has permission node:
        require resolved ALLOW
    else if command is explicitly public:
        allow
    else:
        deny
```

`DENY` must override `ALLOW`.

`UNSET` must not accidentally grant privileged access.

Server console is not a player and does not resolve through player group membership.

Server console authorization is source-based.

---

## Public Server Commands

Some server-bound commands may be public.

A public server-bound command may be executed by any player without a custom permission node.

This should be explicit.

Example:

```cpp
enum class CommandAccess {
    Public,
    PermissionNode,
    ConsoleOnly,
};
```

or:

```cpp
struct Command {
    std::string permission;
    bool public_to_players = false;
    bool console_only = false;
};
```

Avoid implicit public access for server-bound commands.

---

## Console-Only Commands

Some commands may be restricted to the server console.

A console-only command:

* cannot be invoked by players
* does not use player permission resolution
* may perform privileged server lifecycle operations

The dispatcher should reject player execution before argument parsing.

---

## Command Results

Commands return structured results.

```cpp
enum class CommandStatus {
    Success,
    Failure,
    UsageError,
    PermissionDenied,
    UnknownCommand,
    InvalidSource,
    InternalError,
};

enum class CommandMessageKind {
    Info,
    Success,
    Warning,
    Error,
};

struct CommandMessage {
    CommandMessageKind kind;
    std::string text;
};

struct CommandResult {
    CommandStatus status;
    std::vector<CommandMessage> messages;
};
```

Command handlers should not directly write to chat, stdout, or logs except for exceptional diagnostics.

The caller decides how to display results.

---

## Command Errors

Argument parsing should produce useful errors.

Examples:

```text
Expected integer for argument 2, got "abc".
Expected player profile ID, got "Hippo".
Unknown online player: Steve.
Unknown world: test_world.
Relative coordinate requires an anchor.
Permission denied: arvane.command.player.teleport.
```

Errors should be deterministic and testable.

Avoid silent no-op behavior.

---

## Usage Strings

A command may provide an optional usage string.

Example:

```text
/player.teleport <target> <x> <y> <z>
```

Usage strings are documentation and error-output aids.

They should not be the only source of parser behavior.

Eventually, commands may expose structured syntax metadata for autocomplete and richer validation.

---

## Descriptions

A command may provide an optional description.

Descriptions are used by help output, tools, and documentation generators.

The description should explain what the command does, not how it is implemented.

---

## Aliases

Aliases are optional.

An alias maps one input command name to a canonical command name.

Example:

```text
tp -> player.teleport
```

Rules:

* aliases must resolve before permission checks
* aliases must not bypass permissions
* aliases must not duplicate execution functions
* aliases should appear in help output only if intended
* alias loops are invalid

Aliases may be native or configuration-defined.

---

## Data-Driven Command Policy

Native command definitions should be registered in code.

Do not create JSON/YAML files that merely duplicate native command metadata.

This is discouraged:

```json
{
  "name": "time",
  "description": "Reads or changes world time.",
  "permission": "operator"
}
```

The native registration should be the source of truth:

```cpp
registry.register_command({
    .name = "world.time",
    .domain = CommandDomain::ServerBound,
    .permission = "arvane.command.world.time",
    .description = "Reads or changes world time.",
    .usage = "/world.time <...>",
    .execute = execute_world_time_command,
});
```

Data files are useful when they define runtime policy or behavior, such as:

* aliases
* command enable/disable configuration
* permission overrides
* command limits
* macro commands
* scripted commands

Data files should not duplicate static native metadata without adding behavior.

---

## Command Limits

Potentially expensive commands should have explicit limits.

Examples:

* maximum affected blocks
* maximum target selector result count
* maximum radius
* maximum output lines
* cooldown or rate limit
* permission-dependent limit

Limits may be command-specific.

Command limits may be configurable later.

The command handler is responsible for enforcing semantic limits.

The dispatcher is responsible for generic limits such as input length.

---

## Threading Model

Commands must execute on a safe thread for their domain.

Client-bound commands execute through the client command dispatcher on the client thread or another explicitly safe client context.

Server-bound commands execute through the server command dispatcher on the server-authoritative thread or through a scheduled server task.

Input threads must not directly mutate world state.

Recommended server flow:

```text
network/server console input
  ↓
enqueue command request
  ↓
server tick drains command queue
  ↓
command executes against authoritative services
  ↓
command result is queued/sent to output target
```

Commands that need asynchronous work should submit tasks and return an immediate result indicating that work was scheduled, or use a future command-job system if one is later introduced.

Do not block the server tick on expensive command work unless explicitly acceptable.

---

## Networking

The protocol should distinguish chat messages from command requests.

Client behavior:

```text
normal chat text -> ServerboundChatMessage
command input    -> command request packet
```

`ServerboundChatMessage` carries only raw chat intent. It is not a command
transport, and it does not carry rich text formatting.

Planned server-bound command request:

```cpp
struct CommandRequest {
    uint32_t request_id;
    std::string command_text;
};
```

Server response:

```cpp
struct CommandResponse {
    uint32_t request_id;
    CommandResult result;
};
```

Rules:

* server validates the command independently
* server checks permissions independently
* local presentation shortcuts do not apply to authoritative commands
* command result text should be treated as display data, not executable data
* rich text `RunCommand` click events must enter this same command path when a
  remote command transport exists

Implementation note: the current desktop client has an integrated-server command
path but does not yet have a general remote `CommandRequest`/`CommandResponse`
packet. Until that packet exists, slash commands and rich text `RunCommand`
clicks are only authoritative in integrated play.

---

## Registry Ownership

The command registry should be owned by the runtime that executes commands.

Recommended ownership:

```text
ClientRuntime owns client-bound command registry.
ServerRuntime owns server-bound command registry.
```

Integrated singleplayer still has both domains:

* client-bound commands execute in the client runtime
* server-bound commands execute in the integrated server runtime

This keeps singleplayer behavior aligned with multiplayer behavior.

---

## Module Registration

Each module may expose a command registration function.

Examples:

```cpp
void register_client_commands(CommandRegistry& registry);
void register_server_commands(CommandRegistry& registry);
void register_world_commands(CommandRegistry& registry);
void register_player_commands(CommandRegistry& registry);
void register_plugin_commands(CommandRegistry& registry);
```

The command system should provide the framework.

Feature modules should own their command implementations.

The built-in gameplay command implementations live in `arv_base`, the compiled
module declared by the `arvane_base` pack. For example, `/tp` is registered by
`arv_base` while the parser, dispatcher, and registry remain in `arv_command`.
Server startup accepts an optional registrar callback; if no registrar is
provided, the server starts with an empty command registry.

Avoid putting gameplay command behavior into `arv_command`.

---

## Plugin Integration

Plugins may register commands through the command registry.

Plugin command rules:

* plugin command names must not collide with existing commands unless override is explicitly allowed
* plugin permission nodes should use a stable plugin namespace or public plugin domain
* plugin argument validators may be registered for plugin-specific types
* plugin commands must declare their domain
* server-bound plugin commands must use the same permission pipeline as native commands

Example permission node:

```text
plugin.example.command.spawn_arena
```

---

## Autocomplete and Suggestions

Autocomplete is not required for the initial implementation, but the architecture should not prevent it.

Future suggestion providers may use:

* command registry
* argument validators
* player service
* world service
* registry service
* permission service

Suggestions must respect command domain and permission visibility.

A player should not receive suggestions for commands they are not allowed to use unless a policy explicitly allows showing inaccessible commands.

---

## Testing

The command system should have unit tests for:

* command registration
* duplicate registration rejection
* alias resolution
* unknown command handling
* client-bound dispatch
* server-bound dispatch
* permission-denied behavior
* public command behavior
* console-only command behavior
* argument tokenization
* primitive argument parsing
* `Identifier` parsing
* `PlayerProfileID` parsing
* online player lookup
* world lookup by name
* world lookup by UUID
* target selector parsing
* relative coordinate parsing
* relative coordinate failure without anchor
* structured command results

Tests should use fake command services where possible.

Command parser tests should not require a renderer, network transport, or full world runtime.

---

## Implementation Notes

Recommended framework module:

```text
arv_command
```

Possible files:

```text
command.hpp
command_registry.hpp
command_registry.cpp
command_dispatcher.hpp
command_dispatcher.cpp
command_context.hpp
command_args.hpp
command_args.cpp
command_result.hpp
command_source.hpp
arg_validator.hpp
builtin_arg_validators.hpp
builtin_arg_validators.cpp
target_selector.hpp
target_selector.cpp
```

Feature command implementations should live near the systems they control.

Examples:

```text
arv_base/base_commands.cpp
arv_client/client_commands.cpp
arv_world/world_commands.cpp
arv_entity/entity_commands.cpp
```

`arv_server/server_commands.cpp` contains server-owned command helpers such as
the console sender, but it does not register `arv_base` commands directly.

`arv_command` should not depend directly on high-level world, entity, item, or renderer internals.

Use service interfaces or registration-time callbacks to keep dependencies controlled.

---

## Key Rules

* Commands are domain-bound.
* Client-bound commands execute locally and do not require server permission checks.
* Server-bound commands execute on the server and must pass server-side validation.
* Player permissions use the custom permission-node system.
* Server console is a distinct source, not a fake player.
* Player identity should use `arv::crypto::PlayerProfileID` where stable identity matters.
* Player names are convenience lookups for online players.
* Command handlers receive one `CommandCtx`.
* Argument parsing should be typed and extensible.
* Relative coordinates require an anchor.
* Native command metadata belongs in code registration.
* Data files should configure policy, aliases, limits, macros, or scripted behavior, not duplicate native definitions.

---

## Key Takeaway

Commands are a typed, permission-aware execution layer over client and server systems.

Chat, server console, and other frontends submit commands.

The command registry defines what exists.

The dispatcher decides where and whether it may run.

The command context provides the services and arguments needed to execute it safely.
