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
- gameplay-affecting plugin messages
- plugin-provided reliable commands

### Unreliable Latest-Wins

Used for frequent state where old data becomes useless.

Examples:

- client input samples
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
ClientHello
{
    protocol_version;
    client_version;
    supported_compression_modes;
    supported_features;
}
```

Server replies:

```text
ServerHello
{
    protocol_version;
    server_version;
    selected_compression_mode;
    enabled_features;
}
```

### 3. Registry Sync

Server sends registry metadata required for this connection.

The client must not assume local runtime IDs match server runtime IDs.

Registry sync may include:

* block registry hash
* item registry hash
* entity type registry hash
* biome registry hash
* required plugin list
* server-assigned runtime ID tables

### 4. World Join

Client requests entry into a world.

For the current dedicated-server flow, the configured world is server-owned:
`arv_dedicated` selects the world from `server.yaml`, then advertises the
assigned `initial_world_id` in `ServerHello` and sends a `WorldJoinResult`
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
| `0x0200 - 0x02FF` | Registry sync                           |
| `0x0300 - 0x03FF` | World join/leave                        |
| `0x0400 - 0x04FF` | Chunk streaming                         |
| `0x0500 - 0x05FF` | Block updates                           |
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

---

## Core Messages

### `ClientHello`

Reliable ordered.

Sent immediately after transport connection.

### `ServerHello`

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

## Client Input Protocol

Client input is sent as unreliable latest-wins messages.

```text
ClientInputBatch
{
    u64 first_input_tick;
    u16 input_count;
    InputSample inputs[input_count];
}
```

```text
InputSample
{
    u32 buttons;
    i16 move_x;
    i16 move_y;
    i16 look_delta_x;
    i16 look_delta_y;
    u8 selected_hotbar_slot;
}
```

Input batches should include enough recent samples that packet loss does not immediately cause missing input.

The server must validate:

* movement speed
* interaction reach
* selected slot legality
* action cooldowns
* world permissions

The client may predict local movement, but the server owns final position.

---

## Player Correction

Reliable or unreliable latest-wins depending on correction severity.

```text
PlayerCorrection
{
    u64 server_tick;
    EntityNetID entity_id;
    Vec3Fixed position;
    Vec3Fixed velocity;
    Rotation rotation;
    u32 correction_flags;
}
```

Large corrections should be reliable.

Small frequent movement snapshots should be unreliable latest-wins.

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
ChunkData
{
    WorldID world_id;
    ChunkCoord coord;
    u64 chunk_version;          // block/geometry version
    u64 chunk_content_version;  // mesh-relevant content/boundary version
    u64 chunk_light_version;    // serialized section light-buffer version
    bool has_lighting;
    bool lighting_valid;
    BlockStateID blocks[4096];
    LightWord lighting[4096] if has_lighting;
}
```

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
ChunkUnload
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
BlockUpdateBatch
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
EntitySpawn
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
EntityDespawn
{
    EntityNetID entity_id;
    u16 reason;
}
```

### Entity Snapshot

Unreliable latest-wins.

```text
EntitySnapshot
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

### Entity State Delta

Reliable or unreliable depending on field semantics.

Stable gameplay state must be reliable.

Visual-only state may be unreliable.

---

## Inventory and Interaction

Inventory state is reliable ordered.

The client may request interactions, but the server validates and applies them.

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
InventoryUpdate
{
    u32 inventory_id;
    u32 revision;
    u32 slot_count;
    SlotUpdate slots[slot_count];
}
```

Inventory revisions must be monotonic.

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

Chat and commands are reliable ordered.

```text
ChatMessage
{
    string message;
}
```

```text
CommandRequest
{
    string command_line;
}
```

The server must parse and authorize commands.

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
|  `0` | reliable               | `ClientHello`, `ServerHello`, disconnect, world join/leave |
|  `1` | reliable               | block updates, entity lifecycle, inventory, reliable gameplay actions |
|  `2` | unreliable latest-wins | `ClientInputBatch`, `PlayerCorrection`, `EntitySnapshot` |
|  `3` | reliable bulk          | `ChunkData`, `ChunkUnload`, skin image upload/download chunks |
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
* client input older than the server input window is discarded
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
ClientHello
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
ServerHello
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

If the hashes do not match, the server may either:

* reject the connection
* require registry synchronization
* require a matching asset/data pack
* allow connection only in a limited diagnostic mode

Early milestones may reject mismatched registries rather than implementing registry sync.

The protocol must not assume that runtime IDs are stable across client and server until the handshake and registry compatibility step has completed.

## Asset Compatibility

Asset compatibility is separate from protocol compatibility.

A client may speak the correct protocol but still lack required textures, models, fonts, UI definitions, or block/entity data.

The server should be able to advertise required asset pack hashes or data pack hashes during the handshake.

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

1. Registry synchronization or validation
2. Asset/data pack validation
3. Player/session initialization
4. Initial world assignment
5. Initial chunk streaming
6. Entity spawn synchronization
7. Gameplay-ready notification

The client should not assume it is gameplay-ready immediately after `ServerHello`.

The server must explicitly transition the client into a gameplay-ready state.
After `WorldJoinResult`, the client sends `ChunkInterestUpdate` from the
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
