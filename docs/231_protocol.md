# Protocol

This document defines Arvane’s game protocol on top of GameNetworkingSockets.

The networking architecture is defined in `networking.md`. This document defines the actual messages, sequencing rules, replication channels, versioning, and packet semantics used by the client and server.

GameNetworkingSockets provides reliable and unreliable message delivery, encryption, fragmentation/reassembly, connection management, and optional lanes/priorities. It does **not** provide high-level entity replication, delta compression, serialization, or game protocol design; Arvane owns those layers. :contentReference[oaicite:0]{index=0}

---

## Goals

The protocol must be:

- server authoritative
- versioned
- explicit about reliability
- friendly to snapshot/delta replication
- debuggable
- resilient to malformed or outdated clients
- independent from raw C++ memory layout
- suitable for integrated singleplayer and dedicated servers

The protocol must not depend on:

- pointer values
- compiler struct packing
- host endianness
- runtime-only registry IDs without negotiation
- client-trusted world state

---

## Transport Layer

Arvane uses GameNetworkingSockets as the transport.

The engine should use connection-oriented sockets through `ISteamNetworkingSockets`, with servers listening through an IP listen socket and clients connecting by address for normal dedicated-server networking. GameNetworkingSockets exposes listen/connect APIs such as `CreateListenSocketIP` and `ConnectByIPAddress`. :contentReference[oaicite:1]{index=1}

The protocol layer sits above GameNetworkingSockets and is responsible for:

- encoding and decoding messages
- assigning message types
- validating payloads
- mapping messages to replication systems
- enforcing protocol version compatibility
- deciding reliability mode per message class

---

## Reliability Classes

Every message type must declare one of the following reliability classes.

### Reliable Ordered

Used for messages that must arrive and must be processed in order.

Examples:

- login/auth handshake
- registry negotiation
- world join acceptance
- inventory updates
- chat
- chunk data
- chunk unloads
- block changes
- entity spawn/despawn
- local player movement corrections
- local player server-applied velocity impulses
- gameplay-affecting plugin messages
- plugin-provided reliable commands

### Reliable Immediate

Used for tiny, rare control messages that should bypass normal reliable
batching when the transport supports a no-delay reliable send mode.

Examples:

- `ClientboundKickPlayer`
- future administrative disconnect or moderation notices

### Unreliable Latest-Wins

Used for frequent state where old data becomes useless.

Examples:

- `ServerboundPlayerMove`
- player look direction
- entity movement snapshots
- transient physics snapshots
- debug telemetry

### Unreliable Event

Used sparingly for events that are useful but not critical.

Examples:

- cosmetic particles
- sound cues
- animation hints
- non-authoritative visual effects

Important state must not rely on unreliable event messages.

---

## Message Envelope

Every protocol message uses a common envelope.

```text
MessageEnvelope
{
    u16 protocol_version;
    u16 message_type;
    u32 message_flags;
    u32 sequence;
    u32 ack_reference;
    u32 payload_size;
    bytes payload;
}
```

### Field Meaning

| Field              | Meaning                                                          |
| ------------------ | ---------------------------------------------------------------- |
| `protocol_version` | Arvane protocol version, not engine version                      |
| `message_type`     | Numeric message kind                                             |
| `message_flags`    | Compression, encryption metadata, debug flags, etc.              |
| `sequence`         | Per-connection protocol sequence number                          |
| `ack_reference`    | Optional protocol-level reference for diagnostics or custom acks |
| `payload_size`     | Size of payload in bytes                                         |
| `payload`          | Message-specific serialized data                                 |

GameNetworkingSockets already handles transport-level reliability. Arvane sequence numbers are for protocol debugging, stale-message rejection, input ordering, and replication validation.

---

## Encoding Rules

All protocol messages must use explicit serialization.

Rules:

* integers are little-endian
* booleans encode as `u8`
* strings encode as `u16 length + UTF-8 bytes`
* arrays encode as `u32 count + elements`
* optional values encode as `u8 present + value`
* enums encode as fixed-width integers
* no raw structs are sent directly
* no pointers are serialized
* no padding bytes are serialized

Protocol structs may resemble C++ structs, but network encoding is always explicit.

---

## Protocol Versioning

The protocol version is separate from:

* game version
* engine version
* save version
* registry version
* plugin version

The client and server must negotiate protocol compatibility during handshake.

The current protocol version is `3`.

A server may reject a client when:

* the protocol version is unsupported
* required features are missing
* registry negotiation fails
* the client uses incompatible gameplay/plugin definitions

---

## Connection Lifecycle

```text
Disconnected
    -> Connecting
    -> TransportConnected
    -> ProtocolHandshake
    -> RegistrySync
    -> WorldJoin
    -> InGame
    -> Disconnecting
    -> Disconnected
```

### 1. Transport Connected

GameNetworkingSockets establishes the connection.

No gameplay messages are valid yet.

### 2. Protocol Handshake

Client sends:

```text
ServerboundHello
{
    protocol_version;
    client_version;
    supported_compression_modes;
    supported_features;
}
```

Server replies:

```text
ClientboundHello
{
    protocol_version;
    server_version;
    selected_compression_mode;
    enabled_features;
}
```

### 3. Pack and Registry Sync

Server sends registry metadata required for this connection.

The client must not assume local runtime IDs match server runtime IDs.

Pack and registry sync may include:

* block registry hash
* item registry hash
* entity type registry hash
* biome registry hash
* required plugin list
* server-assigned runtime ID tables
* required client pack bundle hashes
* chunked client pack bundle transfer
* server-owned client registry snapshot

Current V1 dedicated-server sync sends `ClientboundPackManifest` after
authentication and before world join. The client compares bundle hashes against
`arvane_home/cache/server-packs/`, requests missing bundles with
`ServerboundPackRequest`, installs verified `.apack` bundles into the
content-addressed cache, verifies `ClientboundRegistrySnapshot`, then replies
with `ServerboundPackReady`. The server does not send
`ClientboundWorldJoinResult` until `ServerboundPackReady` matches the required
bundle set and registry hash.

### 4. World Join

Client requests entry into a world.

For the current dedicated-server flow, the configured world is server-owned:
`arv_dedicated` selects the world from `server.yaml`, then advertises the
assigned `initial_world_id` in `ClientboundHello` and sends a `ClientboundWorldJoinResult`
before the client may enter gameplay. The client must not infer the world from
local save folders or local config.

Server replies with:

* accepted/rejected status
* world ID
* player profile ID
* local entity ID
* spawn position
* initial tick
* simulation settings
* world environment snapshot (`gravity_scale`, atmosphere/oxygen flags, sky, ambient light, fog)
* view/simulation radius limits

### 5. In Game

Normal replication begins.

---

## Message Type Ranges

Message types are grouped by range.

|             Range | Category                                |
| ----------------: | --------------------------------------- |
| `0x0000 - 0x00FF` | Core protocol/control                   |
| `0x0100 - 0x01FF` | Login/session                           |
| `0x0200 - 0x02FF` | Registry and pack sync                  |
| `0x0300 - 0x03FF` | World join/leave                        |
| `0x0400 - 0x04FF` | Chunk streaming                         |
| `0x0500 - 0x05FF` | Block updates and world events          |
| `0x0600 - 0x06FF` | Entity replication                      |
| `0x0700 - 0x07FF` | Player input                            |
| `0x0800 - 0x08FF` | Inventory/interaction                   |
| `0x0900 - 0x09FF` | Chat/commands                           |
| `0x0A00 - 0x0AFF` | UI/plugin menus                         |
| `0x0B00 - 0x0BFF` | Debug/dev tools                         |
| `0x8000 - 0xFFFF` | Reserved for negotiated plugin messages |

Skin/profile messages currently occupy `0x0120 - 0x0131` inside the
login/session range. Metadata/control packets use the session lane; image byte
payloads and chunks use the chunk-streaming bulk lane.

Directional packet names use `ClientboundX` for server-to-client packets and
`ServerboundX` for client-to-server packets. Bidirectional utility packets such
as `Ping`, `Pong`, and `DisconnectReason` keep neutral names.

---

## Pack Sync Messages

Pack sync messages are reliable ordered. Manifest, status, requests, registry
snapshot, and ready notifications use the `Session` lane. Bundle byte chunks use
the `ChunkStreaming` lane.

The V1 message set is:

| Message | Direction | Purpose |
| --- | --- | --- |
| `ClientboundPackManifest` | Server to client | Lists required bundle descriptors, registry hash, maximum bundle size, and total download size. |
| `ServerboundPackSyncStatus` | Client to server | Reports cache hits, needed bundles, loaded bundles, or failure status. |
| `ServerboundPackRequest` | Client to server | Requests one missing bundle by hash. |
| `ClientboundPackDataChunk` | Server to client | Sends a bounded byte slice of a requested `.apack` bundle. |
| `ClientboundRegistrySnapshot` | Server to client | Sends the server-owned client-visible registry snapshot payload and hash. |
| `ServerboundPackReady` | Client to server | Confirms verified bundles and registry snapshot before world join. |

For V1 all advertised bundles are required. A client-side rejection, download
failure, verification failure, registry hash mismatch, or missing loaded bundle
disconnects the session before world join.

---

## Core Messages

### `ServerboundHello`

Reliable ordered.

Sent immediately after transport connection.

### `ClientboundHello`

Reliable ordered.

Confirms protocol compatibility and selected features.

### `DisconnectReason`

Reliable ordered when possible.

```text
DisconnectReason
{
    u16 reason_code;
    string message;
}
```

### `ClientboundKickPlayer`

Reliable immediate on the session lane.
Message ID: `0x0009`.

Sent by the server before an intentional server-side disconnect. The reason is
human-readable UI text and is capped at 512 UTF-8 bytes on the wire. Servers use
this packet for shutdown, duplicate profile rejection, authentication rejection,
protocol rejection, timeout, and future administrative kicks. If the transport
has already reported a failed connection, cleanup may happen without this packet.

```text
ClientboundKickPlayer
{
    string reason;
}
```

### `ClientboundKeepAlive`

Unreliable event on the session lane.
Message ID: `0x000A`.

```text
ClientboundKeepAlive
{
    u64 id;
}
```

### `ServerboundKeepAlive`

Unreliable event on the session lane. The client echoes the latest
`ClientboundKeepAlive.id`.
Message ID: `0x000B`.

```text
ServerboundKeepAlive
{
    u64 id;
}
```

### `Ping`

Unreliable or reliable depending on diagnostics mode.

```text
Ping
{
    u64 client_time;
}
```

### `Pong`

```text
Pong
{
    u64 client_time;
    u64 server_time;
}
```

---

## Tick Model

Server snapshots are associated with a server tick.

```text
ServerTick
{
    u64 tick_index;
    u32 tick_rate;
}
```

The client must treat server tick as authoritative.

Client render interpolation may use snapshot history, but gameplay authority remains on the server.

---

## Player Movement Protocol

Player movement is sent as unreliable latest-wins messages.

```text
ServerboundPlayerMove
{
    u32 sequence;
    u64 client_tick;
    Vec3f position;
    Vec3f velocity;
    f32 yaw_radians;
    f32 pitch_radians;
    u32 movement_flags;
    u16 action_flags;
    u8 selected_hotbar_slot;
}
```

`sequence` is a monotonically increasing client movement packet counter. The
server uses it to reject stale movement. `client_tick` is diagnostic context;
movement guardrails use server receive time, not client-provided elapsed time.

`movement_flags` describes compact locomotion state such as on-ground,
sneaking, sprinting, swimming, and flying. `action_flags` carries edge/held
actions that still require server gameplay validation. Hotbar selection rides on
this packet so the server can process interaction commands with the intended
held slot.

The server must validate:

* movement speed
* movement distance
* finite coordinates and rotation
* loaded destination chunk
* collision sanity
* interaction reach
* selected slot legality
* action cooldowns
* world permissions

Accepted movement updates the server's latest accepted player transform and is
replicated to observers through entity snapshots. Rejected movement sends a
reliable `ClientboundPlayerMoveCorrection`.

---

## Player Movement Send Rate

The client defaults to a 20 Hz movement send rate.

Servers that use a non-default cadence send:

```text
ClientboundSetMovementHz
{
    f32 movement_hz;
}

ClientboundPlayerMovementTuning
{
    f32 fly_speed;
    f32 fly_vertical_speed;
}
```

These packets are reliable ordered session setup data. `ClientboundSetMovementHz`
is sent during world join only when the configured movement send rate differs
from the default; the client applies it to future `ServerboundPlayerMove`
cadence and does not acknowledge it separately. The client also receives a
reliable ordered `ClientboundPlayerPermissions` packet during world join and
whenever the server changes the player's resolved core permissions, including
game mode changes. The server sends `ClientboundPlayerMovementTuning` during
world join and whenever a command such as `/flyspeed` changes player-specific
movement tuning. The packet carries actual blocks-per-second values; the
`/flyspeed` command converts its `1..10` multiplier to those values before the
packet is sent.

---

## Player Movement Correction

Movement corrections are reliable ordered.

```text
ClientboundPlayerMoveCorrection
{
    u32 rejected_sequence;
    Vec3f position;
    Vec3f velocity;
    f32 yaw_radians;
    f32 pitch_radians;
    u32 movement_flags;
    u8 reason;
}
```

The client applies this packet directly to the local player transform and
velocity. There is no per-movement acknowledgement and no rewind/replay step.

Correction reasons include invalid numbers, stale packets, excessive speed,
excessive single-packet distance, collision, unloaded destination chunks,
out-of-world coordinates, missing permissions, server teleports, and unknown
fallbacks.

---

## Player Velocity

Server-applied local player velocity impulses are reliable ordered.

```text
ClientboundPlayerVelocity
{
    u64 server_tick;
    PlayerID player_id;
    Vec3f velocity;
    u32 knockback_control_suppression_ticks;
}
```

The server sends this packet to the owning client when authoritative gameplay
changes that player's velocity without teleporting them. Combat knockback uses
the existing server knockback calculation and sends the resulting velocity plus
the short control-suppression window used by the knockback system.

The client applies the velocity directly to the local movement state. Upward
velocity clears the local on-ground flag. During the suppression window, local
horizontal input is blended with the incoming knockback velocity instead of
immediately overriding it. This packet does not acknowledge movement and does
not imply that older movement packets were accepted.

---

## Player Block-Break Progress

Block-break UI progress is separate from movement correction.

```text
ClientboundPlayerBlockBreakProgress
{
    u64 server_tick;
    PlayerID player_id;
    BlockBreakProgress block_break;
}
```

This packet is transient latest-wins gameplay state. It updates the local
mining/progress presentation and does not imply movement acceptance.

---

## Chunk Streaming

Chunk streaming is server-to-client and server-authoritative.

### Chunk Coordinates

Chunks are addressed by chunk coordinate, not world block coordinate.

```text
ChunkCoord
{
    i32 x;
    i32 y;
    i32 z;
}
```

### Chunk Data

Reliable ordered.

```text
ClientboundChunkData
{
    WorldID world_id;
    ChunkCoord coord;
    u64 chunk_version;          // block/geometry version
    u64 chunk_content_version;  // mesh-relevant content/boundary version
    u64 chunk_light_version;    // serialized section light-buffer version
    bool has_lighting;
    bool lighting_valid;
    bool has_fluids;
    EncodedArray<BlockStateID, 4096> blocks;
    EncodedArray<LightWord, 4096> lighting if has_lighting;
    EncodedArray<FluidCell, 4096> fluids if has_fluids;
}
```

Chunk section arrays are encoded as either dense values or run-length values.
The sender chooses the smaller form independently for blocks, lighting, and
fluids. This keeps sparse fluid sections and uniform air/light sections small
enough for the reliable chunk-streaming lane instead of expanding one fluid cell
into a full dense fluid buffer.

`chunk_light_version` describes the section's own light buffer. It does not advance for dependency-only neighbor `LightMesh` invalidations; those are derived by the receiver when changed boundary light data arrives.

`has_lighting` describes whether a light buffer is present in the payload. `lighting_valid` describes whether that buffer is current authoritative lighting. A dirty-light chunk may still include its previous light buffer with `has_lighting=true` and `lighting_valid=false` so clients can mesh with old lighting instead of inventing full sky or full darkness. When the relight finishes, the server must send a later payload with `lighting_valid=true`, even if the numeric light values did not change.

Chunk payload may contain:

* block state IDs
* lighting data
* biome data
* height/terrain metadata
* block entity data
* optional compression

The client must discard chunk data if:

* it targets an unloaded world
* the chunk is no longer relevant
* the block/geometry version and light version are older than already-applied data
* registry compatibility is missing

Lighting data is server-authoritative. Clients may render and remesh from received lighting, including temporary placeholder/stale lighting, but must not generate authoritative light propagation. If a chunk payload has `has_lighting=false`, the receiver preserves any existing light buffer for that section; only a brand-new section without prior lighting uses a conservative dark placeholder until a later light payload arrives.

### Chunk Unload

Reliable ordered.

```text
ClientboundChunkUnload
{
    ChunkCoord coord;
    u32 reason;
}
```

The client must remove render and collision data for unloaded chunks.

---

## Block Updates

Block updates are reliable ordered.

```text
BlockUpdate
{
    BlockPos pos;
    StateID new_state;
    u32 update_flags;
    u64 server_tick;
}
```

Batch form:

```text
ClientboundBlockUpdateBatch
{
    u64 server_tick;
    u32 update_count;
    BlockUpdate updates[update_count];
}
```

Block updates must be applied after the relevant chunk exists.

If a block update arrives before chunk data, the client may either:

* queue it briefly, or
* request chunk resync

The server remains authoritative.

### Explosion Event

Unreliable event.

`ClientboundExplosionEvent` announces a server-authoritative explosion to nearby
clients for local visual and future sound effects. It does not carry block
mutations, entity damage, or drops; those remain ordinary authoritative world
replication.

```text
ClientboundExplosionEvent
{
    WorldID world_id;
    Vec3Fixed origin;
    string profile_id;
    f32 radius;
    u64 seed;
    bool visual_enabled;
    bool future_sound_enabled;
    string future_sound_id;
}
```

---

## Entity Replication

Entity network IDs are not raw ECS entity IDs.

```text
EntityNetID
{
    u32 value;
}
```

Entity IDs are assigned by the server and scoped to a world/session.

### Entity Spawn

Reliable ordered.

```text
ClientboundEntitySpawn
{
    EntityNetID entity_id;
    EntityTypeID entity_type;
    Vec3Fixed position;
    Rotation rotation;
    u32 spawn_flags;
    bytes initial_state;
}
```

### Entity Despawn

Reliable ordered.

```text
ClientboundEntityDespawn
{
    EntityNetID entity_id;
    u16 reason;
}
```

### Entity Snapshot

Unreliable latest-wins.

```text
ClientboundEntitySnapshot
{
    u64 server_tick;
    u32 entity_count;
    EntitySnapshotEntry entities[entity_count];
}
```

```text
EntitySnapshotEntry
{
    EntityNetID entity_id;
    Vec3Fixed position;
    Vec3Fixed velocity;
    Rotation rotation;
    u32 state_flags;
}
```

### Dropped Item Snapshot

Unreliable latest-wins.

```text
ClientboundDroppedItemSnapshot
{
    WorldID world_id;
    u64 server_tick;
    u32 drop_count;
    DroppedItemSnapshotEntry drops[drop_count];
}
```

```text
DroppedItemSnapshotEntry
{
    EntityNetID drop_id;
    ItemStackWire stack;
    Vec3Fixed position;
    Vec3Fixed velocity;
}
```

### Entity State Delta

Reliable or unreliable depending on field semantics.

Stable gameplay state must be reliable.

Visual-only state may be unreliable.

---

## Inventory and Interaction

Inventory state is reliable ordered.

The client may request interactions, but the server validates and applies them.

### Shared Item Stack Encoding

Inventory updates and dropped-item snapshots encode visible stacks with the same
wire form.

```text
ItemStackWire
{
    u32 item_id;
    u16 count;
    bool has_data;
    if has_data:
        u32 data_byte_count;
        bytes data_binary_document[data_byte_count];
}
```

`data_binary_document` is an `io::DataDocument` encoded with the binary data
codec. `data_byte_count` is capped at 16 KiB per stack. Empty object data is
canonicalized to `has_data=false`.

### Use Item

```text
UseItemRequest
{
    u64 client_input_tick;
    u8 hand;
    u8 selected_slot;
    InteractionTarget target;
}
```

### Inventory Update

```text
ClientboundInventoryUpdate
{
    u32 inventory_id;
    u32 revision;
    u8 selected_hotbar_slot;
    u32 slot_count;
    OptionalItemStack slots[slot_count];
    OptionalItemStack carried_stack;
}
```

Inventory revisions must be monotonic.

Data-only item stack mutations must advance the inventory revision.

---

## Player Profile And Skin Packets

Skin/profile packets are explicit protocol messages. They never transfer base64
data and never identify skins by filename.

### Types

```cpp
enum class SkinImageFormat : u8 { Png = 0 };
enum class SkinUploadMode : u8 { Full = 0, Chunked = 1 };

struct SkinDescriptor {
    string skin_hash; // empty means fallback/default
    SkinImageFormat format;
    u16 width;
    u16 height;
    bool slim_model;
};

struct PlayerProfileInfo {
    PlayerID player_id;
    string profile_id;
    string display_name;
    SkinDescriptor skin;
};
```

### Message IDs

| ID       | Packet                                  | Lane              |
| -------: | --------------------------------------- | ----------------- |
| `0x0120` | `ClientboundSkinRules`                  | Session           |
| `0x0121` | `ClientboundPlayerProfileInfo`          | Session           |
| `0x0122` | `ClientboundPlayerProfileInfoBatch`     | Session           |
| `0x0123` | `ServerboundSkinRequest`                | Session           |
| `0x0124` | `ServerboundSkinRequestBatch`           | Session           |
| `0x0125` | `ClientboundSkinData`                   | ChunkStreaming    |
| `0x0126` | `ClientboundSkinDataBeginChunked`       | ChunkStreaming    |
| `0x0127` | `ClientboundSkinDataChunk`              | ChunkStreaming    |
| `0x0128` | `ClientboundSkinDataEndChunked`         | ChunkStreaming    |
| `0x0129` | `ClientboundSkinRequestDeny`            | Session           |
| `0x012A` | `ServerboundSkinUploadBegin`            | Session           |
| `0x012B` | `ClientboundSkinUploadAccept`           | Session           |
| `0x012C` | `ClientboundSkinUploadDeny`             | Session           |
| `0x012D` | `ServerboundSkinUploadData`             | ChunkStreaming    |
| `0x012E` | `ServerboundSkinUploadChunk`            | ChunkStreaming    |
| `0x012F` | `ServerboundSkinUploadEnd`              | ChunkStreaming    |
| `0x0130` | `ClientboundSkinUploadComplete`         | Session           |
| `0x0131` | `ClientboundSkinUploadFailed`           | Session           |

### Login Flow

After authentication succeeds, the server sends `ClientboundSkinRules` near
`ClientboundAuthResult`. The packet contains whether skins are enabled, whether
uploads are allowed, the maximum allowed resolution for the authenticated
player, and the server-selected `SkinDescriptor`.

Dedicated clients choose the first local profile skin whose width and height are
`<= max_res`. If that skin differs from the selected descriptor hash or slim
model flag and uploads are allowed, the client sends
`ServerboundSkinUploadBegin`. Integrated singleplayer uses the first local skin
with no effective local restriction.

`SkinDescriptor::slim_model` is presentation metadata for player geometry. It
selects the slim player model for that profile but does not change the PNG hash,
image validation, or storage path. Slim and classic player models use the same
player animation asset.

### Upload Flow

The server validates upload begin before accepting:

* authenticated session
* skins enabled and uploads allowed
* upload-begin and failed-decode rate limits
* no active pending upload for the connection
* global active upload capacity
* PNG format
* square supported resolution
* resolution enabled and permission-allowed
* non-zero compressed byte count within the resolution upload limit
* per-connection upload byte budget

The server selects `Full` for small uploads and `Chunked` for large uploads.
Chunked uploads send `ServerboundSkinUploadChunk*` followed by
`ServerboundSkinUploadEnd`; full uploads send exactly one
`ServerboundSkinUploadData`.

Accepted uploads carry a server-issued `upload_id`. Data/chunk/end packets are
valid only for the owning authenticated session, the matching upload id, and the
accepted upload mode. Pending uploads track start time, last packet time,
expected byte count, received byte count, and mode. The server clears the
pending buffer on timeout, idle timeout, disconnect, mode mismatch, chunk
overflow, byte-count mismatch, or other invalid upload data.

Final validation requires exact compressed byte count, PNG decode success,
claimed dimensions matching decoded dimensions, enabled resolution match,
decoded RGBA8 size within the resolution limit, and an authoritative hash
computed from the original compressed bytes. The server stores files only as
`player_assets/skins/<skin_hash>.png`, writes through a temporary file, atomically
renames into place, updates the selected skin hash and slim-model flag only
after validation succeeds, sends upload complete/failed, and broadcasts updated
profile metadata. Repeated failed decodes rate-limit future upload begins.

### Download Flow

Profile info advertises `SkinDescriptor`; it does not include image bytes. A
client missing a validated cached file sends `ServerboundSkinRequest`. Servers
only honor hashes previously advertised to that client by profile metadata when
`require_advertised_hash` is enabled. Requests are per-session rate-limited,
batch size is capped, duplicate in-flight skin requests are rejected or ignored,
and each connection has an in-flight request limit plus a skin-download byte
budget. Small files may use `ClientboundSkinData`; large files use:

```text
ClientboundSkinDataBeginChunked
ClientboundSkinDataChunk*
ClientboundSkinDataEndChunked
```

Clients treat server skin bytes as untrusted. They cache downloaded bytes only
after request id, requested hash, computed hash, PNG decode, dimensions, and
byte counts validate. Cache files are written by hash only, through a temporary
file, and invalid transfers are discarded in favor of the fallback/default skin.

The client must discard stale inventory revisions.

---

## Chat and Commands

Chat is split into client intent and server-authored presentation. Chat packets
are reliable ordered because chat is visible, low-frequency, and order-sensitive.

```text
ServerboundChatMessage
{
    string message;
    optional string channel;
}
```

`message` is raw player-authored text. The client must not include rank
prefixes, colors, hover events, click events, JSON rich text, or permission
claims. The server validates and rate-limits the message before routing it.

```text
ClientboundChatMessage
{
    uint64 message_id;
    string channel;
    ChatMessageKind kind;
    RichText message;
}
```

`message` is the final server-authored rich text tree. The rich text model,
color/style semantics, flattening, validation, and default chat formatter live
in `arv_text`; this protocol section only describes how those text payloads are
carried on the wire. A payload may include RGB color style patches, inherited
text styles, `ShowText` hover events, and click events such as
`SuggestCommand`, `RunCommand`, `OpenUrl`, and `CopyToClipboard`. Clients render
this payload safely and apply local safeguards such as URL confirmation before
attempting platform actions.

Commands are distinct from chat. Slash-prefixed input should route through the
normal command path, where the server parses and authorizes privileged
behavior.

The client must not execute privileged command behavior locally.

---

## Plugin Protocol Messages

Plugins may define custom protocol messages only through negotiated message ranges.

Rules:

* plugin messages must be declared in plugin metadata
* message type ranges must be assigned by the server
* clients must reject unknown plugin messages unless explicitly marked optional
* plugin messages must follow the same serialization rules as core messages
* plugin messages must declare reliability class

Example metadata:

```yaml
network:
  messages:
    - name: ExamplePlugin.OpenMenu
      direction: server_to_client
      reliability: reliable_ordered
      schema: example_plugin_open_menu
```

---

## UI/Menu Replication

Server-driven UI messages are reliable ordered.

This allows plugins and server systems to open menus on the client without giving the server arbitrary rendering control.

```text
OpenMenu
{
    u32 menu_id;
    string menu_type;
    bytes menu_definition;
}
```

```text
CloseMenu
{
    u32 menu_id;
}
```

```text
MenuAction
{
    u32 menu_id;
    string action;
    bytes payload;
}
```

The server validates all menu actions.

---

## Compression

Compression is negotiated during handshake.

Compression may be used for:

* chunk data
* registry sync
* large entity state blobs
* plugin payloads

Compression should not be used for tiny frequent messages unless profiling proves it helps.

The protocol envelope should indicate whether the payload is compressed.

---

## Fragmentation

Large logical messages should be avoided when possible.

GameNetworkingSockets supports message fragmentation/reassembly, but Arvane should still keep message sizes reasonable and prefer chunked/batched transfer for large data. ([GitHub][1])

Examples:

* send chunks individually or in bounded batches
* split large registry sync into sections
* avoid enormous plugin payloads
* avoid sending full world state in one message

---

## Lanes and Priorities

GameNetworkingSockets supports multiple message lanes for head-of-line blocking control and bandwidth sharing. Arvane uses lanes to separate critical gameplay traffic from bulk streaming traffic. ([GitHub][1])

Current core lanes:

| Lane | Reliability            | Message types                                 |
| ---: | ---------------------- | --------------------------------------------- |
|  `0` | reliable/control       | `ServerboundHello`, `ClientboundHello`, `ClientboundKickPlayer`, keep alive, `ClientboundSetMovementHz`, `ClientboundPlayerPermissions`, `ClientboundPlayerMovementTuning`, world join/leave |
|  `1` | reliable               | block updates, entity lifecycle, inventory, reliable gameplay actions |
|  `2` | mixed movement         | `ServerboundPlayerMove`, `ClientboundEntitySnapshot`, `ClientboundPlayerMoveCorrection`, `ClientboundPlayerVelocity` |
|  `3` | reliable bulk          | `ClientboundChunkData`, `ClientboundChunkUnload`, skin image upload/download chunks |
|  `4` | mixed/debug            | ping/pong, diagnostics, dev traffic           |

Chunk streaming must not starve input, entity snapshots, or critical reliable messages.
Chunk stream state is only considered sent after the transport accepts the reliable send;
backpressure or send-buffer failure must leave the chunk pending for a later retry.

---

## Security and Validation

The server must treat all client messages as untrusted.

The server validates:

* protocol version
* message type
* payload size
* enum ranges
* string length
* array counts
* player permissions
* world membership
* entity ownership
* interaction reach
* inventory revisions
* movement limits

Malformed messages may result in:

* ignored message
* warning log
* client correction
* disconnect
* ban/kick policy handled by higher-level server logic

---

## Stale Data Rules

Messages with ticks, revisions, or versions must reject stale data.

Examples:

* older chunk block/light versions are discarded
* older inventory revision is discarded
* older entity snapshot is discarded
* stale player movement sequences are rejected or ignored
* stale entity snapshots are discarded by tick
* block update targeting an unloaded chunk is queued briefly or rejected

---

## Debugging Requirements

Protocol debugging should include:

* message type names
* byte sizes
* reliability class
* send/receive counts
* disconnect reason
* protocol version mismatch details
* registry hash mismatch details
* malformed payload diagnostics
* per-lane bandwidth stats where available

Debug logging must avoid dumping sensitive auth/session tokens.

---

## Forbidden Patterns

The protocol must not:

* serialize raw C++ structs directly
* trust client-side block edits
* trust client-side inventory edits
* expose raw ECS entity IDs
* depend on client registry order
* assume reliable messages are free
* send full snapshots every tick without profiling
* let plugin messages bypass validation
* let debug messages mutate gameplay state in production builds

---

## Implementation Notes

Recommended modules:

```text
arv_net_protocol/
    protocol_version.hpp
    message_type.hpp
    message_envelope.hpp
    message_reader.hpp
    message_writer.hpp
    protocol_error.hpp
    protocol_dispatcher.hpp
    compression.hpp
    schemas/
        core_messages.hpp
        chunk_messages.hpp
        entity_messages.hpp
        input_messages.hpp
        inventory_messages.hpp
```

The protocol layer should not own gameplay state.

It should decode messages and hand validated commands/events to higher-level systems.

---

## Client/Server Handshake Payloads

The protocol handshake establishes whether a client and server can communicate safely before world data, chunks, entities, or gameplay state are exchanged.

The handshake must be explicit, versioned, and small.

The handshake is not responsible for transferring full registries, assets, chunks, entities, or save data. It only establishes compatibility and identifies what follow-up synchronization steps are required.

## Handshake Goals

The handshake should determine:

* whether the client and server use compatible protocol versions
* whether the client can understand the server’s registry layout
* whether the client has compatible asset/data definitions
* whether the server accepts the client session
* which world/session the client should enter
* which optional protocol features are enabled

The server is authoritative. The client may advertise capabilities, but the server decides what is accepted.

## Client Hello Payload

The first client handshake payload should include:

```text
ServerboundHello
    protocol_version
    client_build_id
    client_name_or_brand
    supported_compression_modes
    supported_protocol_features
    registry_hashes_known_by_client
    asset_pack_hashes_known_by_client
    requested_username_or_profile_name
    session_token_or_placeholder
```

Field expectations:

* `protocol_version` identifies the network protocol version.
* `client_build_id` identifies the client build for diagnostics.
* `client_name_or_brand` is informational and must not be trusted for authority.
* `supported_compression_modes` lists compression formats the client can decode.
* `supported_protocol_features` lists optional features supported by the client.
* `registry_hashes_known_by_client` allows the server to detect mismatched registries.
* `asset_pack_hashes_known_by_client` allows the server to detect missing or outdated client assets.
* `requested_username_or_profile_name` is a display/session request, not proof of identity.
* `session_token_or_placeholder` reserves space for future authentication/session handling.

Authentication may remain a placeholder during early milestones, but the payload shape should not need to be redesigned later.

## Server Hello Payload

The server response should include:

```text
ClientboundHello
    accepted
    reject_reason
    protocol_version
    server_build_id
    server_name
    selected_compression_mode
    enabled_protocol_features
    authoritative_registry_hashes
    authoritative_asset_pack_hashes
    assigned_session_id
    assigned_player_id
    initial_world_id
    required_followup_sync
```

Field expectations:

* `accepted` indicates whether the connection may continue.
* `reject_reason` is populated when `accepted` is false.
* `protocol_version` confirms the selected protocol version.
* `server_build_id` identifies the server build for diagnostics.
* `server_name` is informational.
* `selected_compression_mode` is chosen by the server from the client-supported set.
* `enabled_protocol_features` lists the final enabled feature set.
* `authoritative_registry_hashes` describes the server’s loaded gameplay registries.
* `authoritative_asset_pack_hashes` describes required client-facing assets/data.
* `assigned_session_id` identifies the client’s active server session.
* `assigned_player_id` identifies the logical player controlled by the client.
* `initial_world_id` identifies the world/dimension the player should enter first.
* `required_followup_sync` tells the client what must be synchronized before joining gameplay.

## Registry Compatibility

The server should compare client-provided registry hashes against authoritative server registry hashes.

If the hashes match, gameplay may continue after normal session setup.

If the hashes do not match, the server may either reject the connection or
require pack/registry synchronization before world join. The current dedicated
server path uses the pack-sync phase for required client content and registry
snapshot verification.

The protocol must not assume that runtime IDs are stable across client and server until the handshake and registry compatibility step has completed.

## Asset Compatibility

Asset compatibility is separate from protocol compatibility.

A client may speak the correct protocol but still lack required textures, models, fonts, UI definitions, or block/entity data.

The server can advertise required asset/data content through
`ClientboundPackManifest` during the follow-up sync phase.

Early milestones may use a single coarse asset hash. Later versions may split this into more granular hashes, such as:

```text
blocks_hash
items_hash
entities_hash
textures_hash
models_hash
ui_hash
```

## Rejection Reasons

Handshake rejection should be explicit.

Common rejection reasons include:

```text
unsupported_protocol_version
missing_required_feature
unsupported_compression
registry_mismatch
asset_mismatch
server_full
invalid_session
banned_or_denied
internal_server_error
```

The client should display or log a useful message for rejection, but the server should avoid leaking sensitive internal details.

## Handshake Follow-Up

After a successful handshake, the connection may proceed through additional synchronization phases.

Possible follow-up phases include:

1. Asset/data pack synchronization and validation
2. Registry snapshot synchronization or validation
3. Player/session initialization
4. Initial world assignment
5. Initial chunk streaming
6. Entity spawn synchronization
7. Gameplay-ready notification

The client should not assume it is gameplay-ready immediately after `ClientboundHello`.

The server must explicitly transition the client into a gameplay-ready state.
After `ClientboundWorldJoinResult`, the client sends `ServerboundChunkInterestUpdate` from the
server-assigned spawn/body position. The server then owns chunk generation and
chunk payload delivery for the joined world.

---

## Relationship to Other Documents

This document should be read alongside:

* [Networking](230_networking.md)
* [World and Chunks](70_world_and_chunks.md)
* [Blocks and Registry](90_blocks_and_registry.md)
* [Entities](180_entities.md)
* [Inventory and Interaction](170_inventory_and_interaction.md)
* [Tick System](80_tick_system.md)
* [Save Format](200_save_format.md)
* [UI System](111_ui_system.md)
* [Plugin API](260_plugin_api.md)
