# Chat and Rich Text Messaging

## Intent

Arvane chat is a server-authoritative rich text messaging system.

The client sends raw player intent. The server validates that intent, applies permissions and moderation rules, formats the final message, and sends a structured rich text payload back to clients.

This allows servers to control chat presentation, including rank prefixes, player display names, colors, hover text, clickable actions, channel formatting, private message formatting, system messages, and plugin-provided decorations.

Chat messages are not plain strings with embedded formatting codes. They are structured text components.

## Goals

- Allow server-controlled chat formatting.
- Support colored text.
- Support clickable text segments.
- Support hoverable text segments.
- Allow rank prefixes, badges, player metadata, and plugin-provided decorations.
- Keep the client responsible only for rendering and interaction.
- Prevent clients from injecting unauthorized formatting.
- Keep command execution permission-checked on the server.
- Make the rich text model reusable for other UI messages, such as disconnect reasons, system notices, and tooltips.
- Keep the protocol safe, bounded, and versionable.

## Non-goals

- Clients do not send fully formatted chat messages to other players.
- Clients do not decide rank prefixes, display colors, or server chat layout.
- Click events do not bypass command permissions.
- Server-provided text never executes arbitrary client code.
- Chat formatting is not encoded through legacy inline color codes in the network payload.
- Chat is not a replacement for the command system.

## Naming conventions

Packet names must follow Arvane packet naming conventions:

```text
ServerboundX => client sends X to server
ClientboundX => server sends X to client
```

The core chat packets are:

```cpp
ServerboundChatMessage
ClientboundChatMessage
```

`ServerboundChatMessage` carries raw player chat input from the client to the server.

`ClientboundChatMessage` carries final formatted rich text from the server to the client.

## Authority model

The server is authoritative over network-visible chat.

The client may own:

* Chat input UI.
* Local chat history display.
* Text wrapping and rendering.
* Hover hit testing.
* Click hit testing.
* Local-only informational messages.
* User accessibility overrides.
* Link confirmation UI.

The server owns:

* Whether a player may chat.
* Whether a message is accepted.
* Rate limiting.
* Moderation and filtering.
* Recipient selection.
* Channel selection.
* Rank prefixes.
* Display names.
* Server-side colors.
* Server-side hover events.
* Server-side click events.
* Final broadcast format.

The client must not trust another client’s requested formatting. A player’s submitted chat message is only text until the server formats it.

## Message flow

Normal player chat flow:

```text
Client text input
  -> ServerboundChatMessage
  -> server validation
  -> server moderation/filtering
  -> server formatting
  -> recipient selection
  -> ClientboundChatMessage
  -> client rich text layout/rendering
```

The client sends:

```cpp
ServerboundChatMessage {
    message = "hello"
}
```

The server broadcasts:

```cpp
ClientboundChatMessage {
    channel = "arvane:global",
    kind = ChatMessageKind::Chat,
    message = RichText(...)
}
```

The client should display the server’s returned rich text. The client should not locally invent the final broadcast format.

## Reliability

Chat packets should use reliable ordered delivery.

Chat is low-frequency, user-visible, and order-sensitive. Lost or reordered chat messages are more harmful than the small cost of reliable delivery.

Recommended reliability layer:

```text
ServerboundChatMessage  -> reliable ordered
ClientboundChatMessage  -> reliable ordered
```

## Core packets

### `ServerboundChatMessage`

Sent by the client when the player submits normal chat text.

```cpp
struct ServerboundChatMessage {
    std::string message;
    std::optional<std::string> channel;
};
```

`message` is raw user-authored text.

`channel` is optional. If omitted, the server chooses the default channel for the player. If provided, the server still validates that the player may send to that channel.

The client must not include:

* Rank prefixes.
* Display name formatting.
* Chat colors.
* Hover events.
* Click events.
* JSON rich text.
* Server-side metadata.
* Permission claims.

The server must treat `message` as untrusted input.

### `ClientboundChatMessage`

Sent by the server when a message should be displayed to a client.

```cpp
struct ClientboundChatMessage {
    ChatMessageId message_id;
    std::string channel;
    ChatMessageKind kind;
    RichText message;
};
```

`message_id` is a server-assigned identifier for this delivered message. It is useful for future deletion, moderation, acknowledgements, debugging, and local history management.

`channel` is a stable namespaced channel identifier.

Examples:

```text
arvane:global
arvane:system
arvane:private_message
arvane:party
```

`kind` describes the broad message category.

```cpp
enum class ChatMessageKind : u8 {
    Chat,
    System,
    Announcement,
    PrivateMessage,
    Error
};
```

`message` is the final server-formatted rich text payload.

## Future chat packets

The following packet names are reserved for likely future features:

```cpp
ClientboundClearChat
ClientboundRemoveChatMessage
ClientboundChatChannelList
ClientboundChatChannelState
```

`ClientboundClearChat` clears part or all of the local chat history.

`ClientboundRemoveChatMessage` removes or hides a specific server-assigned chat message.

`ClientboundChatChannelList` sends available channels to the client.

`ClientboundChatChannelState` updates mute/filter/visibility state for one or more channels.

These should not be implemented until there is a concrete use case.

## Rich text model

Chat messages use a tree of rich text components.

A rich text node contains:

* Optional literal text.
* Optional style patch.
* Child nodes.
* Optional click event.
* Optional hover event.

Conceptual structure:

```cpp
struct RichText {
    std::string text;
    TextStylePatch style;
    std::vector<RichText> children;

    std::optional<ClickEvent> click_event;
    std::optional<HoverEvent> hover_event;
};
```

The root node may have empty text and only contain children.

Example display:

```text
[Admin] Hippo: hello
```

Example human-readable representation:

```json
{
  "text": "",
  "children": [
    {
      "text": "[Admin] ",
      "style": {
        "color": "#ff5555",
        "bold": true
      },
      "hover_event": {
        "action": "show_text",
        "value": {
          "text": "Server Administrator"
        }
      }
    },
    {
      "text": "Hippo",
      "style": {
        "color": "#ffffff"
      },
      "click_event": {
        "action": "suggest_command",
        "value": "/msg Hippo "
      },
      "hover_event": {
        "action": "show_text",
        "value": {
          "text": "Click to message Hippo"
        }
      }
    },
    {
      "text": ": hello",
      "style": {
        "color": "#dddddd"
      }
    }
  ]
}
```

The wire format does not need to be JSON. JSON examples in this document are only illustrative.

## Style inheritance

Styles are inherited from parent nodes.

A style field that is absent means “inherit from the parent.”

A style field that is present overrides the inherited value.

Use a style patch, not a fully resolved style, in serialized rich text:

```cpp
struct TextStylePatch {
    std::optional<RgbColor> color;

    std::optional<bool> bold;
    std::optional<bool> italic;
    std::optional<bool> underlined;
    std::optional<bool> strikethrough;
    std::optional<bool> obfuscated;

    std::optional<std::string> font;
};
```

The renderer resolves inherited style into a concrete style while flattening the tree for layout.

This matters for boolean decorations. A child must be able to explicitly disable a decoration inherited from a parent.

Example:

```json
{
  "text": "",
  "style": {
    "bold": true
  },
  "children": [
    {
      "text": "bold "
    },
    {
      "text": "not bold",
      "style": {
        "bold": false
      }
    }
  ]
}
```

## Colors

Network chat colors should use explicit RGB colors.

```cpp
struct RgbColor {
    u8 r;
    u8 g;
    u8 b;
};
```

The server may define colors using config names, rank definitions, permission groups, or theme files, but the final network payload should resolve those colors to RGB.

The client applies chat opacity, fade-out opacity, disabled-state opacity, and accessibility overrides locally. Those are not part of the server-authored text color.

Avoid alpha in server-authored chat colors. Alpha-controlled text can produce invisible or misleading messages. If opacity is needed, it should be a client display concern.

## Formatting codes

The network payload must not use legacy inline formatting codes as the canonical representation.

Do not send this over the protocol:

```text
§c[Admin] §fHippo: hello
```

Do not send this as the canonical representation:

```text
&c[Admin] &fHippo: hello
```

Formatting codes may be accepted in server config files or plugin convenience APIs, but they must be parsed and converted into structured `RichText` before being sent to clients.

Player-authored chat text should not be parsed for formatting unless the server explicitly supports permission-gated formatting.

## Click events

Click events are attached to individual rich text nodes.

```cpp
struct ClickEvent {
    ClickAction action;
    std::string value;
};
```

Supported actions:

```cpp
enum class ClickAction : u8 {
    OpenUrl,
    SuggestCommand,
    RunCommand,
    CopyToClipboard
};
```

### `OpenUrl`

Opens an external URL.

Rules:

* The client must show a confirmation screen before opening the URL.
* The client should allow only safe schemes, such as `http` and `https`.
* The server must not be able to silently open a browser.
* The server must not be able to open local files.
* The server must not be able to execute shell commands.

### `SuggestCommand`

Places text into the chat input box without sending it.

Example:

```json
{
  "action": "suggest_command",
  "value": "/msg Hippo "
}
```

This is useful for player names, reply actions, report actions, and moderation tools.

### `RunCommand`

Runs a command as though the player submitted it manually.

Rules:

* The command must route through the normal command system.
* The server must perform the normal permission checks.
* The click event must not bypass command validation.
* The client should not execute commands locally unless the command is explicitly client-domain and registered as safe.

For server-domain commands, the click should result in normal command submission behavior.

### `CopyToClipboard`

Copies text to the local clipboard.

Rules:

* Clipboard writes must happen only after a user click.
* The server must not be able to write to the clipboard without user interaction.
* The client may show a small confirmation notification.

## Hover events

Hover events are attached to individual rich text nodes.

```cpp
struct HoverEvent {
    HoverAction action;
    HoverValue value;
};
```

Supported actions:

```cpp
enum class HoverAction : u8 {
    ShowText,
    ShowItem,
    ShowEntity
};
```

### `ShowText`

Displays a rich text tooltip.

```json
{
  "action": "show_text",
  "value": {
    "text": "Click to message this player"
  }
}
```

`ShowText` should support nested rich text, but it must still obey size and depth limits.

### `ShowItem`

Displays an item tooltip from structured item data.

The server should send a bounded snapshot of the item tooltip data. The client should render that data using normal item tooltip UI.

The server should not send arbitrary UI layout instructions.

Example conceptual payload:

```json
{
  "action": "show_item",
  "value": {
    "item": "arvane:iron_sword",
    "count": 1,
    "display_name": {
      "text": "Iron Sword",
      "style": {
        "color": "#ffffff"
      }
    }
  }
}
```

### `ShowEntity`

Displays an entity tooltip from structured entity data.

Example conceptual payload:

```json
{
  "action": "show_entity",
  "value": {
    "entity_type": "arvane:pig",
    "display_name": {
      "text": "Pig"
    }
  }
}
```

The payload should be a snapshot, not a live reference to arbitrary server state.

## Rich text limits

Rich text payloads must be bounded.

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

The server should enforce these limits before sending.

The client should also validate received payloads defensively.

A malformed or excessive rich text payload should be rejected, truncated, or replaced with a safe error message.

## Server chat validation

The server must validate every `ServerboundChatMessage`.

Recommended config:

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

Server validation should handle:

* Empty messages.
* Overly long messages.
* Control characters.
* Newlines if disabled.
* Invalid UTF-8.
* Excessive whitespace.
* Spam.
* Muted players.
* Channel permissions.
* Server-specific moderation rules.

The server should never trust the client to self-limit.

## Unicode handling

Chat text should be valid UTF-8.

The server should reject or sanitize invalid UTF-8.

The client renderer should process text by grapheme clusters where practical, especially for:

* Cursor movement.
* Selection.
* Deletion.
* Line wrapping.
* Hit testing.

The chat protocol should avoid assuming one byte equals one character.

## Commands and chat

Chat and commands should use separate packet paths.

Normal chat:

```cpp
ServerboundChatMessage
```

Commands:

```cpp
ServerboundCommand
```

The client may use this rule for the input box:

```text
input starts with "/" -> ServerboundCommand
otherwise             -> ServerboundChatMessage
```

The server should still validate all command input. The leading slash is only a client UI convention.

Click events with `RunCommand` must route through the same command system as manually typed commands.

## Server-side formatting pipeline

The server formatting pipeline should be explicit.

Recommended flow:

```text
1. Receive ServerboundChatMessage.
2. Validate raw text.
3. Check chat permissions.
4. Check channel permissions.
5. Apply moderation/filtering.
6. Build ChatFormatContext.
7. Format final RichText.
8. Select recipients.
9. Send ClientboundChatMessage.
10. Write server log entry.
```

The formatter should receive structured context:

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
```

The formatter returns a `RichText` message.

```cpp
class ChatFormatter {
public:
    RichText format_player_chat(const ChatFormatContext& context);
    RichText format_system_message(const SystemMessageContext& context);
    RichText format_private_message(const PrivateMessageContext& context);
};
```

## Player-authored text insertion

Player-authored text must be inserted as literal text.

If the player sends:

```text
{"text":"fake admin message"}
```

The server must treat it as literal text, not rich text.

If the player sends:

```text
&cRed text
```

The server must treat it as literal text unless the server has explicitly enabled permission-gated formatting for that player.

Player input should never be concatenated into serialized rich text without escaping.

## Chat channels

Channels should use stable namespaced identifiers.

Examples:

```text
arvane:global
arvane:local
arvane:system
arvane:private_message
arvane:party
```

A channel controls routing and filtering. It should not by itself define final visual formatting.

The server may choose different formats for different channels, but formatting should still be produced through the normal rich text pipeline.

Channel data may include:

```cpp
struct ChatChannelDefinition {
    std::string id;
    RichText display_name;

    bool player_sendable;
    bool client_visible;
    bool logged;

    std::optional<std::string> required_send_permission;
    std::optional<std::string> required_receive_permission;
};
```

The client may use channel metadata for filtering, tabs, or settings, but the server remains authoritative over which messages are delivered.

## Server configuration

Server chat formatting may be configured through data, code, plugins, or a combination of these.

A YAML-backed default format is appropriate for user-facing configuration.

Example:

```yaml
chat:
  validation:
    max_message_codepoints: 256
    allow_newlines: false

  channels:
    arvane:global:
      display_name: "Global"
      player_sendable: true
      logged: true

  formats:
    player_chat:
      children:
        - rank_prefix: true

        - text: " "

        - player_name: true
          color: "#ffffff"
          click:
            action: suggest_command
            value: "/msg {player_name} "
          hover:
            action: show_text
            text: "Click to message {player_name}"

        - text: ": "
          color: "#777777"

        - message: true
          color: "#dddddd"
```

This config is not the network format. It is a server-side authoring format that compiles into `RichText`.

## Rank prefixes

Rank prefixes should be produced server-side.

A rank prefix should be represented as rich text, not as a raw string.

Example:

```json
{
  "text": "[Admin]",
  "style": {
    "color": "#ff5555",
    "bold": true
  },
  "hover_event": {
    "action": "show_text",
    "value": {
      "text": "Server Administrator"
    }
  }
}
```

This allows rank prefixes to have:

* Color.
* Bold/italic/underline.
* Hover text.
* Click actions.
* Multiple segments.
* Icons or glyphs later if supported by the font system.

## Private messages

Private messages should still use `ClientboundChatMessage`.

They should use a private-message channel and kind:

```cpp
channel = "arvane:private_message";
kind = ChatMessageKind::PrivateMessage;
```

The server should send separately formatted messages to the sender and receiver if needed.

Example receiver display:

```text
[From Hippo] hello
```

Example sender display:

```text
[To Steve] hello
```

Both are server-authored rich text messages.

## System messages

System messages should be sent through `ClientboundChatMessage` unless they require a dedicated UI packet.

Examples:

* Player joined.
* Player left.
* Server restart warning.
* Permission error.
* Command feedback.
* Moderation notice.

System messages should use `ChatMessageKind::System`, `Announcement`, or `Error` depending on purpose.

## Disconnect and kick messages

The `RichText` type should be reusable for disconnect reasons and kick reasons.

For example:

```cpp
ClientboundKickPlayer {
    RichText reason;
}
```

Disconnect screens may render rich text, but they should use a restricted interaction profile.

Recommended disconnect screen behavior:

* Allow colors and basic formatting.
* Allow hover text if useful.
* Disable `RunCommand`.
* Require confirmation for `OpenUrl`.
* Consider disabling `CopyToClipboard` unless explicitly useful.

This keeps disconnect messages visually consistent with chat without allowing unsafe actions.

## Client rendering

The client should flatten the rich text tree into styled text runs.

Rendering pipeline:

```text
RichText tree
  -> resolve inherited styles
  -> flatten into styled segments
  -> shape text with font system
  -> wrap lines
  -> store glyph bounds
  -> render
  -> hit test hover/click regions
```

Each rendered segment should preserve its source interaction metadata.

The client needs enough layout information to answer:

* Which segment is under the mouse?
* Does that segment have a hover event?
* Does that segment have a click event?
* What text range was clicked?
* What tooltip should be shown?

Line wrapping must not lose click or hover boundaries.

## Font and shaping interaction

Rich text styling may split text into multiple shaping runs.

The renderer should shape each run using the active font, style, and fallback stack.

The renderer should avoid splitting inside grapheme clusters.

For practical chat usage, it is acceptable that style boundaries also become shaping boundaries.

## Chat history

The client owns local chat history display.

The server owns delivered message content.

The client may store:

```cpp
struct ChatHistoryEntry {
    ChatMessageId message_id;
    std::string channel;
    ChatMessageKind kind;
    RichText message;

    TimePoint received_at;
};
```

The client may apply local settings:

* Chat opacity.
* Chat scale.
* Chat width.
* Line spacing.
* Fade delay.
* Passive-chat visible duration.
* Passive-chat maximum visible lines.
* Open-chat maximum visible lines.
* Maximum retained messages.
* Channel visibility.
* Profanity visibility if client-side filtering is added.
* Accessibility color overrides.

Local settings must not mutate the authoritative message payload.

When chat input is closed, the client should display only the recent on-screen
subset of history. That passive overlay may fade messages out after the
configured visible duration and cap the number of visible lines. The passive
chat background should be drawn whenever at least one message is currently
visible, and omitted when the on-screen subset is empty.

Opening chat shows retained history again, independent of passive fade state,
up to the configured retained-history and open-chat display limits.

## Link handling

The client must not automatically open URLs.

For `OpenUrl` click events:

1. Player clicks link.
2. Client shows confirmation dialog.
3. Dialog displays the full URL.
4. Player confirms or cancels.
5. Client opens the URL only after confirmation.

The client should reject dangerous schemes.

Allowed by default:

```text
https
http
```

Rejected by default:

```text
file
javascript
data
shell
```

The client should avoid hiding the final URL behind misleading display text in the confirmation dialog.

## Security rules

The chat system must obey these rules:

* Client-submitted chat is untrusted.
* Client-submitted chat does not contain authoritative formatting.
* Server-sent rich text is still bounded and validated by the client.
* Click events never execute arbitrary native code.
* URLs require confirmation.
* Commands always pass through the command system.
* Command permissions are checked on the server.
* Clipboard writes require explicit user action.
* Hover payloads are size-limited.
* Rich text depth is limited.
* Rich text node count is limited.
* Invalid UTF-8 is rejected or sanitized.
* Server logs should preserve plain text and metadata, not only styled output.

## Server logging

The server should log chat in a plain, searchable form.

Recommended log fields:

```cpp
struct ChatLogEntry {
    TimePoint time;
    std::string channel;
    PlayerProfileId sender_profile_id;
    std::string sender_name;
    std::string plain_message;
    std::vector<PlayerProfileId> recipients;
};
```

The server may also log the formatted rich text for audit/debugging, but plain text should remain available.

Logs should distinguish:

* Player-authored message text.
* Server-authored formatting.
* System messages.
* Command feedback.
* Private messages.

## Moderation

Moderation should happen before formatting.

Possible moderation steps:

* Mute checks.
* Channel permission checks.
* Rate limits.
* Message length checks.
* Profanity filtering.
* Link filtering.
* Spam detection.
* Staff-only channel enforcement.

Moderation may produce a `ClientboundChatMessage` back to the sender with `ChatMessageKind::Error`.

Example:

```text
You are sending messages too quickly.
```

This error message is server-authored rich text.

## Plugin integration

Plugins should be able to participate in chat without owning the packet format.

Useful extension points:

```cpp
enum class ChatDecision {
    Continue,
    Cancel
};

struct ChatPreprocessEvent {
    PlayerSessionId sender;
    std::string channel;
    std::string message;
};

struct ChatFormatEvent {
    ChatFormatContext context;
    RichTextBuilder builder;
};

struct ChatBroadcastEvent {
    RichText message;
    std::vector<PlayerSessionId> recipients;
};
```

Plugins may:

* Cancel messages.
* Modify raw message text before formatting.
* Add rank prefixes.
* Add badges.
* Add hover details.
* Add click actions.
* Change recipients.
* Route messages to custom channels.

Plugins must not bypass validation limits.

Plugin-produced rich text must be validated before broadcast.

## RichTextBuilder

Server code should use a builder API instead of manually constructing nested objects everywhere.

Example:

```cpp
RichText message = RichTextBuilder()
    .append("[Admin] ", Style::red().bold())
    .hover_text("Server Administrator")
    .append("Hippo", Style::white())
    .click_suggest_command("/msg Hippo ")
    .hover_text("Click to message Hippo")
    .append(": ", Style::gray())
    .append_literal(player_message, Style::light_gray())
    .build();
```

Builder APIs should make the safe path easy:

* `append_literal` for player-authored text.
* `append_component` for trusted server-authored rich text.
* Explicit click helpers.
* Explicit hover helpers.
* Automatic validation before build or send.

## Plain text extraction

The rich text system should support plain text extraction.

Use cases:

* Server logs.
* Accessibility.
* Copying chat lines.
* Debugging.
* Search.
* Crash reports.
* Tests.

Example:

```cpp
std::string to_plain_text(const RichText& text);
```

For:

```text
[Admin] Hippo: hello
```

The plain text output should be:

```text
[Admin] Hippo: hello
```

Click and hover metadata should not appear in plain text unless explicitly requested by a debug tool.

## Serialization

The serialized form must preserve:

* Text.
* Child order.
* Style patches.
* Click events.
* Hover events.

The serialized form must reject:

* Excessive depth.
* Excessive node count.
* Excessive string size.
* Unknown required event actions.
* Invalid enum values.
* Invalid color encodings.
* Invalid UTF-8.

Unknown optional fields may be ignored for forward compatibility if the protocol version allows it.

## Versioning

Rich text should be versioned as part of the protocol.

When adding a new click or hover action:

1. Add a protocol enum value.
2. Define exact behavior.
3. Define security rules.
4. Define client fallback behavior.
5. Add serialization tests.
6. Add compatibility tests if older clients may connect.

Unknown click actions should be ignored.

Unknown hover actions should be ignored.

Unknown style fields should be ignored unless marked required by a future protocol version.

## Accessibility

The client may provide accessibility overrides.

Examples:

* Disable obfuscated text.
* Force high contrast colors.
* Disable italics.
* Increase chat scale.
* Increase line spacing.
* Disable chat fading.
* Require confirmation for all click actions.
* Disable external links.
* Show URLs inline in link confirmation dialogs.

Accessibility overrides affect local rendering only. They do not change the server-authored message payload.

## Testing requirements

The chat system should have unit tests for:

* `ServerboundChatMessage` validation.
* Message length limits.
* Invalid UTF-8 handling.
* Rate limit behavior.
* Plain text extraction.
* Rich text serialization roundtrip.
* Style inheritance.
* Explicit style reset, such as inherited bold set to false.
* Click event serialization.
* Hover event serialization.
* Rich text depth limits.
* Rich text node count limits.
* Player-authored text escaping.
* Command click events routing through command permissions.
* URL confirmation behavior at the UI boundary.
* Chat formatter output for default player chat.
* Chat formatter output with rank prefixes.
* Channel permission checks.
* Plugin-produced rich text validation.

Integration tests should cover:

* Client sends raw chat.
* Server formats final rich text.
* Multiple clients receive the same formatted message.
* Muted player cannot broadcast.
* Private message sends different formatted output to sender and receiver.
* Clickable player name suggests `/msg <name> `.
* Hoverable rank prefix displays rank details.

## Default behavior

A default server should support:

* Global chat.
* System messages.
* Error messages.
* RGB text colors.
* Bold text.
* Italic text.
* Underlined text.
* `ShowText` hover events.
* `SuggestCommand` click events.
* `OpenUrl` click events with confirmation.

The following can be added after the core path is stable:

* `RunCommand`.
* `CopyToClipboard`.
* `ShowItem`.
* `ShowEntity`.
* Channel lists.
* Chat deletion.
* Chat clearing.
* Advanced plugin formatting.
* Permission-gated player formatting.

## Example default player chat

Raw client input:

```text
hello
```

Server context:

```text
player name: Hippo
rank: Admin
channel: arvane:global
```

Rendered output:

```text
[Admin] Hippo: hello
```

Human-readable rich text:

```json
{
  "text": "",
  "children": [
    {
      "text": "[Admin]",
      "style": {
        "color": "#ff5555",
        "bold": true
      },
      "hover_event": {
        "action": "show_text",
        "value": {
          "text": "Server Administrator"
        }
      }
    },
    {
      "text": " "
    },
    {
      "text": "Hippo",
      "style": {
        "color": "#ffffff"
      },
      "click_event": {
        "action": "suggest_command",
        "value": "/msg Hippo "
      },
      "hover_event": {
        "action": "show_text",
        "value": {
          "text": "Click to message Hippo"
        }
      }
    },
    {
      "text": ": ",
      "style": {
        "color": "#777777"
      }
    },
    {
      "text": "hello",
      "style": {
        "color": "#dddddd"
      }
    }
  ]
}
```

## Implementation boundaries

Shared protocol layer:

* Defines `ServerboundChatMessage`.
* Defines `ClientboundChatMessage`.
* Defines `RichText`.
* Defines `TextStylePatch`.
* Defines `ClickEvent`.
* Defines `HoverEvent`.
* Defines serialization and validation helpers.

Server layer:

* Receives `ServerboundChatMessage`.
* Validates player text.
* Applies permissions and rate limits.
* Formats rich text.
* Broadcasts `ClientboundChatMessage`.
* Logs chat.

Client layer:

* Sends raw chat input.
* Receives `ClientboundChatMessage`.
* Stores local chat history.
* Renders rich text.
* Handles hover events.
* Handles click events safely.
* Displays URL confirmation dialogs.

UI layer:

* Provides chat panel.
* Provides input box.
* Provides tooltip rendering.
* Provides link confirmation screen.
* Provides copy feedback.
* Provides accessibility overrides.

Command layer:

* Owns command parsing.
* Owns command permission checks.
* Handles commands submitted manually or through click events.

## Design rule

The core rule for Arvane chat is:

```text
Clients send intent.
Servers send presentation.
Clients render presentation safely.
```

This keeps chat customizable for servers while preserving authority, security, and clean client behavior.
