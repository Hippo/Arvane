# Implement Server-Authoritative Rich Text Chat

This ExecPlan is a living document. Keep the `Progress`, `Discoveries`, `Decision Log`, and `Outcomes & Retrospective` sections current as work proceeds.

This plan must be self-contained. A future agent should be able to resume from this file without relying on external conversation history.

## Purpose / Big Picture

Implement Arvane's server-authoritative chat system.

Clients send raw chat intent. The server validates, moderates, formats, routes, and broadcasts the final message as structured rich text. The client renders server-authored rich text safely, including colors, hover events, and click events.

You will find a detailed specification in `310_chat.md` in the `docs/` folder.

The core rule is:

```text
Clients send intent.
Servers send presentation.
Clients render presentation safely.
```

The implementation must support:

* `ServerboundChatMessage`
* `ClientboundChatMessage`
* Structured `RichText`
* Chat colors
* Style inheritance
* Hover events
* Click events
* Server-controlled chat formatting
* Rank prefixes and player name decorations
* Safe URL confirmation
* Chat input and chat history UI
* Server validation and rate limiting
* Tests and documentation updates

Packet naming must follow Arvane conventions:

```text
ServerboundX => client sends X to server
ClientboundX => server sends X to client
```

Do not invent alternate packet names.

## Progress

* [x] Read all relevant existing docs before modifying code.
* [x] Locate existing protocol, packet registration, serialization, and reliability mapping code.
* [x] Locate existing client UI/chat/input systems.
* [x] Locate existing server session, player profile, permissions, command, and networking code.
* [x] Add shared rich text protocol types.
* [x] Add serialization/deserialization for rich text and chat packets.
* [x] Add rich text validation and size/depth limits.
* [x] Add `ServerboundChatMessage`.
* [x] Add `ClientboundChatMessage`.
* [x] Assign reliable ordered delivery to chat packets.
* [x] Add server-side chat validation.
* [x] Add server-side chat rate limiting.
* [x] Add server-side chat formatting.
* [x] Add global chat broadcast.
* [x] Add system/error message helpers.
* [x] Add client chat history storage.
* [x] Add client rich text layout/rendering.
* [x] Add color/style rendering.
* [x] Add hover hit testing.
* [x] Add `ShowText` hover tooltip support.
* [x] Add click hit testing.
* [x] Add `SuggestCommand` click support.
* [x] Add `OpenUrl` click support with confirmation.
* [x] Add `RunCommand` click support routed through the normal command path.
* [x] Add `CopyToClipboard` click support if a platform clipboard abstraction exists.
* [x] Add safe fallbacks for unsupported hover/click actions.
* [x] Add unit tests.
* [x] Add integration tests where practical.
* [x] Update relevant documentation after implementation.
* [x] Run formatting, build, and tests.
* [x] Record final results in `Outcomes & Retrospective`.

## Discoveries

Record facts discovered while inspecting the codebase.

* Existing packet IDs live in `engine/arv_protocol/include/arv/protocol/message_type.hpp`.
* Existing packet registration and reliability/lane metadata lives in `engine/arv_protocol/src/message_registry.cpp`.
* Existing serialization uses explicit `MessageWriter` / `MessageReader` helpers in `engine/arv_protocol/include/arv/protocol/message_writer.hpp`.
* Existing client receive dispatch lives in `engine/arv_client/src/client_network_messages.cpp`.
* Existing client send helpers live in `engine/arv_client/src/client_protocol_send.cpp`.
* Existing server receive dispatch lives in `engine/arv_server/src/server_network_messages.cpp`.
* Existing server send helper is `arv::server::detail::send_protocol_message` in `engine/arv_server/src/server_network_messages.cpp`.
* Existing chat UI is plain string local history in `engine/arv_client/src/client_ui_state.cpp` and `engine/arv_ui/src/widgets/chat.cpp`.
* Existing desktop chat command handling is integrated-server-only in `apps/arv_desktop/src/desktop_chat_commands.cpp`.
* There is no implemented server-bound command protocol packet yet, despite `docs/300_command_system.md` describing future command request/response packets.
* Existing UI text rendering supports per-element color, but not full inline rich text spans inside one text element; chat rich text must be flattened into per-run UI elements.
* Existing UI hit testing returns `UiAction` by stable element id and action id, which can be used for chat hover/click runs.
* Existing platform layer has no browser-opening abstraction.
* Existing platform layer has no general clipboard abstraction; clipboard use is limited to ImGui debug tooling.
* Existing player profile identifier type is `arv::crypto::PlayerProfileID`.
* Existing online session display names are available on `arv::server::ServerSession::display_name`.
* Existing rate limiting utility is `arv::core::TokenBucket`.
* Existing UI renderer does not support inline typography decorations in one text node; chat colors and metadata render per run, while bold/italic/underline/strikethrough/obfuscated flags are serialized and preserved for future renderer support.

## Decision Log

Record implementation decisions here as they are made.

* Decision: Use RGB colors in the network payload, not named colors.
  Rationale: Prevent client/server color-token mismatch and keep chat presentation server-controlled.

* Decision: Treat player-authored chat as literal text.
  Rationale: Prevent clients from injecting unauthorized formatting, hover events, or click events.

* Decision: Use reliable ordered delivery for chat packets on the gameplay lane.
  Rationale: Chat is low-frequency, visible to users, and order-sensitive; the existing transport already provides this class.

* Decision: Implement URL confirmation UI but do not open browsers yet.
  Rationale: No safe platform/browser-opening abstraction exists; the spec allows confirmation plus safe logging/ignore instead of unsafe platform code.

* Decision: Preserve and safely ignore `CopyToClipboard` clicks for now.
  Rationale: No general platform clipboard abstraction exists outside debug ImGui tooling.

* Decision: Route `RunCommand` clicks through the existing desktop command path.
  Rationale: The authoritative server-bound command packet described in `docs/300_command_system.md` has not been implemented yet; using the existing path keeps integrated play functional without inventing a one-off transport.

## Outcomes & Retrospective

Implemented the first server-authoritative rich text chat pass:

* Added shared protocol chat/rich text types, bounded validation, serialization, and deserialization.
* Added `ServerboundChatMessage` and `ClientboundChatMessage` with reliable ordered gameplay-lane registration.
* Added server-side chat validation, UTF-8/length/control-character checks, per-session rate limiting, default global-channel formatting, system/error helper messages, and authenticated global broadcast.
* Added client chat history storage for authoritative rich messages and changed normal chat submission to send raw serverbound intent without local echo.
* Added UI rendering for rich chat runs, RGB colors, hover/click metadata, `ShowText` tooltips, `SuggestCommand`, URL confirmation for `OpenUrl`, and safe fallbacks for unsupported actions.
* Added protocol/server/client/UI/integration tests covering packet metadata, rich text round trips and limits, validation/rate limiting, client send/receive behavior, rich UI run actions, and integrated server broadcast.
* Updated chat/protocol/UI/network/command documentation to describe the implemented intent/presentation split.

Intentionally deferred:

* Opening URLs in the OS browser after confirmation, because no platform browser-opening abstraction exists yet.
* Writing to the OS clipboard for `CopyToClipboard`, because no general platform clipboard abstraction exists yet.
* Remote authoritative command request/response packets. Slash commands and `RunCommand` clicks continue to use the existing integrated desktop command path until the command protocol is implemented.
* Inline font decoration rendering for bold/italic/underline/strikethrough/obfuscated flags. These style fields are serialized, validated, inherited, and preserved, but the current UI renderer only applies per-run color.
* Structured item/entity hover payloads. `ShowText` is fully implemented; unsupported hover actions deserialize as bounded fallback text and are ignored safely by the UI.

## Context and Orientation

Arvane uses a server-authoritative multiplayer model. Chat must follow that model.

The client must not broadcast formatted chat. The client sends only raw player intent:

```cpp
ServerboundChatMessage {
    message = "hello"
}
```

The server produces the final presentation:

```cpp
ClientboundChatMessage {
    channel = "arvane:global",
    kind = ChatMessageKind::Chat,
    message = RichText(...)
}
```

The final rendered output may look like:

```text
[Admin] Hippo: hello
```

But internally this must be structured rich text with separate segments for the rank prefix, player name, separator, and message body.

The server must be able to freely insert:

* Rank prefixes
* Badges
* Player display names
* Colors
* Hover details
* Click actions
* Channel-specific formatting
* Plugin-provided decorations, if plugin hooks already exist

Do not implement legacy inline formatting codes as the network representation.

Do not send canonical chat formatting as:

```text
§c[Admin] §fHippo: hello
```

or:

```text
&c[Admin] &fHippo: hello
```

If formatting-code support already exists for configs, parse it server-side into `RichText` before sending to clients. Player-authored text must remain literal unless a permission-gated feature explicitly allows formatting.

## Required Architecture

### Shared protocol types

Add shared protocol/data types for rich text.

Use names close to these unless existing project naming style requires minor adjustments:

```cpp
struct RgbColor {
    u8 r;
    u8 g;
    u8 b;
};

struct TextStylePatch {
    std::optional<RgbColor> color;

    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underlined;
    std::optional<bool> strikethrough;
    std::optional<bool> obfuscated;

    std::optional<std::string> font;
};

enum class ClickAction : u8 {
    OpenUrl,
    SuggestCommand,
    RunCommand,
    CopyToClipboard
};

struct ClickEvent {
    ClickAction action;
    std::string value;
};

enum class HoverAction : u8 {
    ShowText,
    ShowItem,
    ShowEntity
};
```

Represent hover values as a tagged/variant payload.

At minimum, implement `ShowText` fully:

```cpp
struct HoverShowText {
    RichText text;
};
```

If item/entity tooltip systems already exist, implement structured `ShowItem` and `ShowEntity`. If they do not exist, deserialize and ignore unsupported actions safely, or implement a simple safe textual fallback without building a full item/entity tooltip system.

Define the rich text tree:

```cpp
struct RichText {
    std::string text;
    TextStylePatch style;
    std::vector<RichText> children;

    std::optional<ClickEvent> click_event;
    std::optional<HoverEvent> hover_event;
};
```

The actual C++ layout may use project-specific containers and naming conventions, but it must preserve:

* Literal text
* Style patch
* Ordered children
* Optional click event
* Optional hover event

### Chat message types

Add:

```cpp
using ChatMessageId = u64;
```

or use an existing strongly typed ID pattern if the project has one.

Add:

```cpp
enum class ChatMessageKind : u8 {
    Chat,
    System,
    Announcement,
    PrivateMessage,
    Error
};
```

Add packet:

```cpp
struct ServerboundChatMessage {
    std::string message;
    std::optional<std::string> channel;
};
```

Add packet:

```cpp
struct ClientboundChatMessage {
    ChatMessageId message_id;
    std::string channel;
    ChatMessageKind kind;
    RichText message;
};
```

The server must generate `message_id`.

Use stable namespaced channel IDs:

```text
arvane:global
arvane:system
arvane:private_message
```

### Reliability

Register both chat packets as reliable ordered:

```text
ServerboundChatMessage -> reliable ordered
ClientboundChatMessage -> reliable ordered
```

Use the existing GameNetworkingSockets reliability abstraction. Do not add a new transport system.

## Rich Text Validation

Implement rich text limits in shared code.

Recommended defaults:

```cpp
struct RichTextLimits {
    usize max_serialized_size_bytes = 16 * 1024;
    usize max_text_bytes = 4096;
    usize max_nodes = 256;
    usize max_depth = 16;
    usize max_hover_text_bytes = 4096;
    usize max_click_value_bytes = 1024;
};
```

The server must validate rich text before sending it.

The client must validate received rich text defensively before storing or rendering it.

Invalid payload behavior:

* Do not crash.
* Do not disconnect unless the protocol layer already treats invalid packets as fatal.
* Replace with a safe error message or ignore the message.
* Log enough detail for debugging.

Validation must cover:

* Invalid enum values
* Excessive depth
* Excessive node count
* Excessive string length
* Invalid UTF-8
* Oversized click values
* Oversized hover payloads

## Style Inheritance

Implement inherited style resolution.

Rules:

* Missing style fields inherit from parent.
* Present style fields override parent.
* Boolean style fields must support explicit `false`.
* Child nodes can disable inherited bold/italic/etc.

Example behavior:

```json
{
  "style": { "bold": true },
  "children": [
    { "text": "bold" },
    { "text": "not bold", "style": { "bold": false } }
  ]
}
```

The renderer should flatten rich text into styled runs with fully resolved styles.

## Plain Text Extraction

Implement:

```cpp
std::string to_plain_text(const RichText& text);
```

or a project-style equivalent.

This is required for:

* Server logs
* Tests
* Debugging
* Accessibility
* Copying chat lines

Plain text extraction must include node text and children in render order. It must not include hover or click metadata.

## Server Implementation

### Chat service

Add a server-side chat service/manager if one does not already exist.

Suggested responsibilities:

```cpp
class ChatService {
public:
    void handle_chat_message(PlayerSessionId sender, const ServerboundChatMessage& packet);

    void send_system_message(PlayerSessionId recipient, RichText message);
    void send_error_message(PlayerSessionId recipient, RichText message);
    void broadcast_system_message(RichText message);
};
```

Use existing server runtime/session architecture. Do not introduce a global singleton if the project avoids that pattern.

### Validation

Add server validation config:

```cpp
struct ChatValidationConfig {
    usize max_message_bytes = 512;
    usize max_message_codepoints = 256;

    bool allow_newlines = false;
    bool trim_outer_whitespace = true;
    bool collapse_repeated_whitespace = false;

    usize max_messages_per_window = 5;
    Duration rate_limit_window = 3s;
};
```

Apply validation to every `ServerboundChatMessage`.

Validation must handle:

* Invalid UTF-8
* Empty messages
* Overly long messages
* Newlines when disabled
* Control characters
* Excessive whitespace
* Muted/disallowed players if existing permission/moderation state exists
* Invalid or unauthorized channel selection
* Spam/rate limit

If validation fails, send the sender a `ClientboundChatMessage` with:

```cpp
kind = ChatMessageKind::Error;
channel = "arvane:system";
```

### Rate limiting

Implement per-player chat rate limiting.

Default:

```text
5 messages per 3 seconds
```

Be lenient enough for normal play, but prevent obvious spam.

Rate limiting should be server-side only.

### Formatting

Add a server-side formatter.

Suggested shape:

```cpp
struct ChatFormatContext {
    PlayerProfileId sender_profile_id;
    std::string sender_name;
    std::string channel;
    std::string raw_message;

    std::optional<std::string> rank_id;
    std::optional<RichText> rank_prefix;

    bool is_private_message = false;
    bool is_system_message = false;
};

class ChatFormatter {
public:
    RichText format_player_chat(const ChatFormatContext& context);
    RichText format_system_message(std::string_view message);
    RichText format_error_message(std::string_view message);
};
```

The first implementation may use hardcoded defaults, but the structure should allow future config/plugin replacement.

Default player format:

```text
<Hippo> hello
```

If rank data already exists, support:

```text
[Admin] Hippo: hello
```

Do not build a large chat template engine unless there is already a config system ready for this. It is acceptable for this implementation to provide a clean C++ formatter with clear extension points.

### Player-authored text safety

Player-authored text must be inserted as literal text.

If the player sends:

```text
{"text":"fake admin message"}
```

display it as literal text.

If the player sends:

```text
&cRed text
```

display it as literal text unless an explicit permission-gated formatting feature already exists.

Do not deserialize player-authored message text as rich text.

### Broadcasting

For global chat:

1. Receive `ServerboundChatMessage`.
2. Validate.
3. Rate-limit.
4. Resolve sender display data.
5. Format to `RichText`.
6. Assign `ChatMessageId`.
7. Send `ClientboundChatMessage` to all intended recipients.
8. Log plain text.

Recipient selection should use existing online player/session lists.

### Logging

Add plain text logging for chat.

Recommended fields if a structured logger exists:

```cpp
struct ChatLogEntry {
    TimePoint time;
    std::string channel;
    PlayerProfileId sender_profile_id;
    std::string sender_name;
    std::string plain_message;
};
```

If no structured chat log exists, log a clear line through the existing logging system.

Do not log only serialized rich text.

## Command Integration

Chat and commands must remain separate packet paths.

Use:

```cpp
ServerboundChatMessage
```

for normal chat.

Use the existing command packet for commands. Do not rename the command packet unless required by existing docs.

Client behavior:

```text
input starts with "/" -> send command packet
otherwise             -> send ServerboundChatMessage
```

Click events with `RunCommand` must route through the normal command path. They must not bypass server permission checks.

If the command system already supports client-domain and server-domain commands, respect that design:

* Client-domain command: may run locally if registered as safe.
* Server-domain command: send to server through normal command flow.
* Unknown command: handle through existing command error behavior.

## Client Implementation

### Chat input

Add or update the chat input UI.

Requirements:

* Pressing chat key opens input.
* Typing edits input text.
* Enter submits.
* Escape cancels/closes.
* Submitted normal text sends `ServerboundChatMessage`.
* Submitted slash-prefixed text sends through existing command path.
* Empty/whitespace-only messages should not be sent.

Use existing input binding patterns.

### Chat history

Add client-side chat history storage:

```cpp
struct ChatHistoryEntry {
    ChatMessageId message_id;
    std::string channel;
    ChatMessageKind kind;
    RichText message;
    TimePoint received_at;
};
```

Local display settings may affect rendering, but must not mutate the stored authoritative message payload.

Support at minimum:

* Appending received chat messages.
* Displaying recent messages.
* Scrolling when the chat UI is open, if scrolling infrastructure exists.
* Limiting retained history to a reasonable count.

Suggested retained count:

```text
1000 messages
```

Use project config if one already exists.

### Rich text rendering

Render `RichText` as styled runs.

Pipeline:

```text
RichText tree
  -> validate
  -> resolve inherited styles
  -> flatten into styled runs
  -> shape/layout text
  -> wrap lines
  -> render glyphs
  -> store bounds for hit testing
```

Support at minimum:

* RGB color
* Bold if the font renderer supports it
* Italic if the font renderer supports it
* Underline if UI text rendering can draw it
* Strikethrough if UI text rendering can draw it
* Obfuscated text may be implemented as a local visual effect or safely ignored initially
* Font override may be ignored initially if font stacks are not ready, but it must deserialize safely

If bold/italic/underline/strikethrough are not yet supported by the renderer, deserialize and preserve the style fields, then leave a clear TODO and tests for serialization. Do not block the core chat pipeline on full typography support.

### Hover support

Implement hover hit testing over rendered rich text segments.

At minimum:

```cpp
HoverAction::ShowText
```

When hovering over a segment with `ShowText`, display a tooltip using the existing UI tooltip system or a simple chat tooltip panel.

The tooltip must itself render rich text if practical. If recursive rich text rendering is not practical immediately, render the hover text as plain text using `to_plain_text`.

Unsupported hover actions must be ignored safely.

### Click support

Implement click hit testing over rendered rich text segments.

Support:

```cpp
ClickAction::SuggestCommand
ClickAction::OpenUrl
ClickAction::RunCommand
ClickAction::CopyToClipboard
```

#### `SuggestCommand`

Place the event value into the chat input box without sending it.

If chat input is closed, open it and populate the input.

#### `OpenUrl`

Show a confirmation screen before opening.

Requirements:

* Display the full URL.
* Allow confirm/cancel.
* Only open after explicit confirmation.
* Allow only `http` and `https` by default.
* Reject `file`, `javascript`, `data`, shell-like schemes, or unknown schemes.
* Use existing platform/browser-opening abstraction if present.
* If no browser-opening abstraction exists, implement the confirmation UI and log/ignore the final open action with a TODO rather than adding unsafe platform code.

#### `RunCommand`

Route through normal command submission behavior.

Do not execute server-domain commands locally.

Do not bypass permissions.

#### `CopyToClipboard`

Copy only after the click.

Use existing clipboard abstraction if present.

If no clipboard abstraction exists, safely ignore and log a TODO. Do not add a broad platform abstraction unless that is consistent with the project structure.

## Optional/Future Features

Do not overbuild these unless the supporting systems already exist.

Allowed safe stubs:

* `ShowItem`
* `ShowEntity`
* `ClientboundClearChat`
* `ClientboundRemoveChatMessage`
* `ClientboundChatChannelList`
* `ClientboundChatChannelState`
* YAML chat format templates
* Full plugin event hooks

If plugin infrastructure already exists, add clean chat extension points. If not, do not implement a plugin system as part of this plan.

## Tests

Add tests in the existing test framework.

Arvane uses GoogleTest/GoogleMock where applicable. Follow existing test organization.

### Required unit tests

Add tests for:

* `ServerboundChatMessage` serialization roundtrip.
* `ClientboundChatMessage` serialization roundtrip.
* `RichText` serialization roundtrip.
* RGB color serialization.
* Style patch serialization.
* Explicit inherited style reset, such as `bold = false`.
* `ClickEvent` serialization.
* `HoverEvent::ShowText` serialization.
* Plain text extraction.
* Rich text max depth enforcement.
* Rich text max node count enforcement.
* Rich text max text bytes enforcement.
* Invalid enum handling.
* Invalid UTF-8 handling if the project has UTF-8 validation utilities.
* Player-authored text treated as literal text.
* Chat validation rejects empty messages.
* Chat validation rejects overly long messages.
* Chat validation rejects newlines when disabled.
* Chat rate limiting.

### Required server tests

Where practical, add server-level tests for:

* Client sends raw chat and server broadcasts formatted rich text.
* Muted/disallowed sender cannot broadcast if mute/permission state exists.
* Invalid channel is rejected.
* Unauthorized channel is rejected.
* Error response uses `ChatMessageKind::Error`.
* Default formatter produces expected plain text.
* Rank prefix formatting works if rank data exists.

### Required client tests

Where practical, add client/UI-level tests for:

* Rich text flattening preserves order.
* Style inheritance resolves correctly.
* Click event metadata survives layout.
* Hover event metadata survives layout.
* `SuggestCommand` populates chat input.
* `OpenUrl` requires confirmation.
* Unknown click/hover actions are ignored safely.

If the UI system does not currently support automated interaction tests, add unit tests for the non-rendering layout/flattening logic and document what remains manual.

## Documentation Updates

Update relevant documentation after implementation.

At minimum, update or create docs covering:

* Chat system architecture.
* Protocol packets.
* Rich text format.
* Click event safety rules.
* Hover event behavior.
* Server-side validation/rate limiting.
* Client rendering responsibilities.
* Command integration.
* Reliability classification.

Search the docs for outdated references to plain string chat and update them.

Do not leave documentation describing the old behavior as current behavior.

If there is an existing networking/protocol document, add:

```text
ServerboundChatMessage -> reliable ordered
ClientboundChatMessage -> reliable ordered
```

If there is an existing command document, add that slash-prefixed chat input routes through the command packet/path and that `RunCommand` click events use normal command permissions.

If there is an existing UI document, add notes on chat input, rich text rendering, hover tooltips, click actions, and URL confirmation.

## Concrete Steps

### 1. Inspect existing systems

Read the relevant docs and code before changing anything.

Find:

* Packet definitions.
* Packet registration.
* Serialization helpers.
* Protocol enum tables.
* Reliability mappings.
* Server session/player connection code.
* Client network receive dispatch.
* Client UI text renderer.
* Client input handling.
* Command packet/path.
* Permission system, if implemented.
* Player profile/display name system.
* Existing chat code, if any.

Update `Discoveries` with exact files and relevant constraints.

### 2. Add shared rich text model

Add `RgbColor`, `TextStylePatch`, `ClickEvent`, `HoverEvent`, `RichText`, `ChatMessageKind`, and `ChatMessageId`.

Keep these in the shared protocol or shared UI/protocol-neutral layer used by both client and server.

Avoid making server code depend on client UI code.

### 3. Add serialization

Implement serialization/deserialization for all new shared types.

Follow existing project packet serialization style.

Add tests immediately after serialization is implemented.

### 4. Add validation helpers

Implement rich text limits and validation.

Add tests for depth, node count, text size, click value size, hover payload size, and invalid enum values.

### 5. Add packets

Add:

```cpp
ServerboundChatMessage
ClientboundChatMessage
```

Register them in the protocol.

Assign reliable ordered delivery.

Add packet roundtrip tests.

### 6. Implement server chat service

Add or update the server chat service.

Implement handling for `ServerboundChatMessage`.

Apply validation and rate limiting before formatting.

### 7. Implement default formatter

Implement a default `ChatFormatter`.

Minimum default output:

```text
<player> message
```

Better default output if rank/prefix data already exists:

```text
[Rank] Player: message
```

Attach useful default interactions:

* Player name click: `SuggestCommand` with `/msg <player> `
* Player name hover: `ShowText` with simple player information
* Rank prefix hover: `ShowText` with rank name, if rank data exists

Do not fake rank data if no rank system exists.

### 8. Broadcast formatted chat

Broadcast `ClientboundChatMessage` to intended recipients.

Use:

```cpp
channel = "arvane:global"
kind = ChatMessageKind::Chat
```

Generate monotonically increasing `ChatMessageId` values per server process.

### 9. Add server system/error messages

Add helpers for sending server-authored messages:

```cpp
send_system_message(...)
send_error_message(...)
broadcast_system_message(...)
```

Use `ClientboundChatMessage`.

### 10. Implement client receive path

Handle `ClientboundChatMessage` on the client.

Validate the rich text payload defensively.

Append valid messages to chat history.

Ignore or safely replace invalid messages.

### 11. Implement rich text flattening/layout

Add a client-side rich text flattening step that resolves style inheritance and preserves interaction metadata.

Use this for chat rendering.

Do not lose click/hover metadata during wrapping.

### 12. Render chat messages

Render chat history using the existing UI/font system.

Support colors immediately.

Support text decorations when the renderer supports them.

Leave narrow TODOs only for renderer features that are genuinely unsupported.

### 13. Implement hover behavior

Add hit testing for chat text runs.

Implement `ShowText` tooltip display.

Ignore unsupported hover actions safely.

### 14. Implement click behavior

Add hit testing for chat text runs.

Implement:

* `SuggestCommand`
* `OpenUrl` with confirmation
* `RunCommand`
* `CopyToClipboard` if clipboard support exists

All external or privileged actions require explicit user interaction.

### 15. Implement chat input

Add or update chat input behavior.

Submit normal text as `ServerboundChatMessage`.

Submit slash-prefixed input through the existing command path.

### 16. Add tests

Add the required tests listed above.

Prefer fast unit tests for serialization, validation, formatting, and rich text flattening.

Add integration tests where existing infrastructure supports them.

### 17. Update docs

Update all relevant documentation.

Do this before marking the plan complete.

### 18. Final validation

Run the standard project validation commands.

Use the project’s documented commands. If no single standard exists, run the closest equivalents:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Also run formatting/linting commands if the project documents them.

Update `Outcomes & Retrospective`.

## Acceptance Criteria

The implementation is complete when:

* `ServerboundChatMessage` exists and is registered.
* `ClientboundChatMessage` exists and is registered.
* Both chat packets use reliable ordered delivery.
* Clients can submit normal chat text.
* Server validates raw chat text.
* Server rate-limits chat.
* Server formats the final message.
* Server broadcasts `ClientboundChatMessage`.
* Client receives and stores chat history.
* Client renders rich text chat with colors.
* Client preserves style inheritance correctly.
* Client supports `ShowText` hover events.
* Client supports `SuggestCommand` click events.
* Client supports `OpenUrl` click events with confirmation.
* Client supports `RunCommand` through the normal command path.
* Client supports or safely ignores `CopyToClipboard` depending on platform support.
* Player-authored text cannot inject rich text formatting.
* Invalid rich text payloads do not crash the client.
* Server logs plain chat text.
* Tests cover serialization, validation, formatting, and core client/server behavior.
* Documentation is updated to reflect the new system.
* No outdated docs describe chat as plain string broadcast if that is no longer true.

## Idempotence and Recovery

This plan should be safe to resume.

If partially implemented:

1. Check `Progress`.
2. Run tests to identify the current breakage.
3. Inspect packet registration to avoid duplicate packet IDs/names.
4. Inspect serialization tests to verify wire compatibility.
5. Continue from the first incomplete milestone.

If a packet or type was added with the wrong name, rename it to match the required convention rather than keeping aliases.

Required packet names:

```cpp
ServerboundChatMessage
ClientboundChatMessage
```

Do not leave duplicate names such as:

```cpp
ChatMessagePacket
SendChatMessage
ReceiveChatMessage
ClientChatPacket
ServerChatPacket
```

unless they are internal helper names and not protocol packet names.

If rich text rendering is partially complete, keep serialization and server broadcast tests passing while finishing UI behavior.

If URL opening is unsafe or unsupported, keep the confirmation screen and reject/log the open action rather than opening URLs without confirmation.

## Notes and Constraints

* Do not introduce new third-party dependencies unless the project docs already allow them.
* Use existing Arvane containers/types where appropriate.
* Preserve module boundaries.
* Shared protocol types must not depend on client UI code.
* Server chat formatting must not depend on rendering code.
* Client rendering must not decide server rank prefixes or chat layout.
* Commands must remain permission-checked on the server.
* Use clear TODOs only where a supporting system does not exist.
* Do not build an entire plugin system as part of this change.
* Do not build a complex YAML chat template engine unless the existing config system makes it straightforward.
* Do not parse player-authored chat as JSON or rich text.
* Do not implement unsafe URL, file, shell, or clipboard behavior.

## Example Target Behavior

Player submits:

```text
hello
```

Client sends:

```cpp
ServerboundChatMessage {
    message = "hello",
    channel = std::nullopt
}
```

Server formats and broadcasts:

```cpp
ClientboundChatMessage {
    message_id = 42,
    channel = "arvane:global",
    kind = ChatMessageKind::Chat,
    message = RichText(...)
}
```

Client renders:

```text
[Admin] Hippo: hello
```

Where:

* `[Admin]` is server-authored.
* `Hippo` is server-authored display name text.
* `hello` is escaped/literal player-authored text.
* Hovering `[Admin]` may show rank details.
* Clicking `Hippo` may suggest `/msg Hippo `.
* No client was allowed to inject its own rank prefix, color, hover event, or click event.
