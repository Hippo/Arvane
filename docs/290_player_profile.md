# Player Profiles and Public-Key Authentication

## Intent

Arvane uses player profiles to represent persistent player identity and cross-world player state.

Authentication is based on a public/private key pair. The client owns the private key. The server stores or learns the public key. During login, the server proves that the connecting client controls the matching private key by issuing a challenge that the client must sign.

Arvane does not require a central account server. Player identity is per installation or per server. Global account identity, global username reservation, and cross-server profile portability are out of scope.

This design supports:

* dedicated servers
* integrated singleplayer servers
* local/offline profiles
* server-authoritative gameplay
* duplicate display names
* profile-owned skin lists
* server-selected skin persistence
* future identity-provider extensions without changing the world/entity model

---

## Core Concepts

Arvane separates three concepts:

```text
PlayerProfile
PlayerSession
PlayerEntity
```

### PlayerProfile

A `PlayerProfile` is persistent player-owned state.

Examples:

* stable profile id
* public authentication key
* display name
* selected skin hash
* last world
* last position
* spawn point
* game mode
* permissions
* inventory
* stats
* unlocks

The profile survives disconnects, respawns, world transfers, and entity recreation.

### PlayerSession

A `PlayerSession` is transient runtime connection state.

Examples:

* connection id
* session id
* authenticated state
* active world
* active entity
* pending auth challenge

The session exists only while the player is connected.

### PlayerEntity

A `PlayerEntity` is the active in-world simulation body.

Examples:

* transform
* velocity
* physics state
* health
* collision state
* ECS components

The entity may be destroyed, recreated, transferred between worlds, or respawned without changing the profile id.

---

## Identity Model

Arvane does not use username or display name as identity.

The authoritative player identity is:

```text
PlayerProfileID = 0x + lowercase_hex(hash(public_key_bytes))
```

The display name is mutable, non-authoritative metadata.

This is valid:

```text
profile_id:    0x1111...
display_name:  Hippo

profile_id:    0x2222...
display_name:  Hippo
```

Internally, those players are distinct because their public keys and profile ids are distinct.

---

## ID Format

Profile ids, public keys, private-key filenames derived from profile ids, and skin hashes use lowercase `0x`-prefixed hexadecimal strings.

Example profile id:

```text
0x8b3f7a1d4c9e2f00151b0db7f49c4cb2d7e1c69a0bb47f0fd7a66d6b89d5a421
```

Rules:

* profile ids are derived from public key bytes
* profile ids are stable as long as the key pair does not change
* profile ids are not derived from display name
* ids are serialized as strings
* ids use lowercase hex
* ids include the `0x` prefix
* ids do not contain separators
* ids do not depend on runtime IDs

The `0x` prefix is required for readability in logs, save files, debug tools, protocol traces, and filenames.

---

## Cryptography Boundary

Cryptographic operations are exposed through `arv_crypto`.

Gameplay, world, rendering, protocol, client, and server code may depend on `arv_crypto`, but they should not include third-party crypto headers directly.

Use:

```cpp
#include <arv/crypto/...>
```

Do not include implementation-library headers such as `sodium.h` outside `arv_crypto`.

`arv_crypto` owns:

* crypto initialization
* secure random bytes
* Ed25519 key generation
* Ed25519 signing
* Ed25519 verification
* 256-bit hashing
* lowercase hex encoding
* `0x`-prefixed hex encoding
* profile id derivation

Recommended signature algorithm:

```text
Ed25519
```

Recommended sizes:

```text
public key:  32 bytes
private key: 64 bytes
signature:   64 bytes
```

These sizes should be exposed through constants in `arv_crypto`, not hardcoded throughout the engine.

Example API shape:

```cpp
namespace arv::crypto {

struct Ed25519PublicKey {
    std::array<std::byte, ed25519_public_key_size> bytes;
};

struct Ed25519PrivateKey {
    std::array<std::byte, ed25519_private_key_size> bytes;
};

struct Ed25519Signature {
    std::array<std::byte, ed25519_signature_size> bytes;
};

struct Ed25519KeyPair {
    Ed25519PublicKey public_key;
    Ed25519PrivateKey private_key;
};

Ed25519KeyPair generate_ed25519_keypair();

Ed25519Signature sign_ed25519(
    std::span<const std::byte> message,
    const Ed25519PrivateKey& private_key
);

bool verify_ed25519(
    std::span<const std::byte> message,
    const Ed25519Signature& signature,
    const Ed25519PublicKey& public_key
);

struct Hash256 {
    std::array<std::byte, 32> bytes;
};

Hash256 hash_256(std::span<const std::byte> data);

std::string to_lower_hex(std::span<const std::byte> data);
std::string to_prefixed_lower_hex(std::span<const std::byte> data);

std::string derive_profile_id_from_public_key(
    const Ed25519PublicKey& public_key
);

bool initialize_crypto();

}
```

Application entry points call `arv::crypto::initialize_crypto()` during startup before generating keys, signing, verifying, hashing, or generating random bytes.

Recommended callers:

```text
arv_desktop
arv_dedicated
tests using arv_crypto
```

Do not scatter third-party crypto initialization throughout gameplay or networking code.

---

## Local Profile Storage

Local installations store profile metadata, private keys, local skin files, and downloaded skin cache under `arvane_home`.

```text
arvane_home/
  profiles.json
  keys/
    <profile_id>.pk
  skins/
    <skin_hash>.png
  cache/
    skins/
      <skin_hash>.png
```

### Directory Responsibilities

`profiles.json`:

* JSON array of local profiles
* contains profile metadata
* contains public keys
* contains private key filenames
* contains the profile-owned local skin list
* does not contain private key bytes

`keys/`:

* contains local private key files
* private key filenames should normally match `<profile_id>.pk`
* private keys are loaded through the active profile’s `private_key_file` field

`skins/`:

* contains user-owned local skin files
* these are skins selected/imported by the local user
* these files are eligible for upload when server skin rules allow them

`cache/skins/`:

* contains validated skins downloaded from servers
* remote cache files are not considered profile-owned skins
* downloaded skins must not be copied into `skins/` unless the user explicitly imports them

Client rendering treats disk cache and GPU residency as separate systems. Validated PNG files under `skins/` or `cache/skins/` may be decoded into CPU RGBA8 pixels and uploaded to the renderer’s `PlayerSkins` paged atlas. Evicting a skin from the GPU atlas frees only temporary texture-array space; it must not delete local skin files or cached skin files.

### `profiles.json`

`profiles.json` is a JSON array. Until a profile-selection UI exists, the first entry is the active local profile.

Preferred shape:

```json
[
  {
    "version": 1,
    "display_name": "Hippo",
    "private_key_file": "0x7b851c7188122a3ab9d173548a21a8ea0640575003be964c33aacc893a95cb79.pk",
    "profile_id": "0x7b851c7188122a3ab9d173548a21a8ea0640575003be964c33aacc893a95cb79",
    "public_key": "0xb7b7e282606c08c6ae90eaa10e8cb8442d7f67e4287afbbcf0cc16283a6dd531",
    "skins": [
      {
        "skin_hash": "0x13a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d",
        "format": "png",
        "width": 64,
        "height": 64,
        "file": "0x13a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d.png"
      }
    ]
  },
  {
    "version": 1,
    "display_name": "HippoAlt",
    "private_key_file": "0x8b3f7a1d4c9e2f00151b0db7f49c4cb2d7e1c69a0bb47f0fd7a66d6b89d5a421.pk",
    "profile_id": "0x8b3f7a1d4c9e2f00151b0db7f49c4cb2d7e1c69a0bb47f0fd7a66d6b89d5a421",
    "public_key": "0x2e5c2f90884cb17f47b22cb4ab64efb7ccfa1343e91e8d1923a55db97a187944",
    "skins": []
  }
]
```

Private key bytes must never be written into `profiles.json`.

Private key path resolution:

```text
arvane_home/keys/<private_key_file>
```

Local skin path resolution:

```text
arvane_home/skins/<skin.file>
```

Remote cache path resolution:

```text
arvane_home/cache/skins/<skin_hash>.png
```

### Local Profile Creation

When no local profiles exist, the client may create one automatically.

Flow:

```text
client startup
profiles.json missing or empty
generate Ed25519 key pair
derive profile_id from public key
write keys/<profile_id>.pk
write profiles.json with one profile entry
use generated profile for local/integrated play
```

Changing the display name does not change identity.

Changing the key pair creates a new identity and therefore a new profile id.

---

## Local Player Skins

A local profile owns an ordered skin list in `profiles.json`.

The skin list is ordered by preference. The client selects the first skin that is valid under the current server’s skin rules.

For dedicated server play:

```text
best_skin = first profile skin where width <= server_max_res and height <= server_max_res
```

For integrated singleplayer:

```text
best_skin = first profile skin
```

If no skin is applicable, use the default skin.

Default skin:

```text
arvane_home/assets/textures/default_skin/default.png
arvane:texture/default_skin/default
```

The default skin is used when:

* the profile has no local skins
* no local skin satisfies server rules
* server skins are disabled
* a remote skin is still downloading
* skin validation fails
* the requested skin is not resident in the GPU player-skin atlas
* player-skin atlas allocation or upload fails

The default skin is not uploaded as a custom skin unless the user explicitly imports/selects it as a normal local skin.

---

## Skin Hashes and Storage

Skin hashes are content hashes of the original accepted PNG bytes:

```text
skin_hash = "0x" + lowercase_hex(hash(original_png_bytes))
```

Rules:

* client may compute hashes for local lookup, cache lookup, and upload hints
* server computes the authoritative hash from uploaded bytes
* client-supplied hashes are never authoritative
* skin filenames are hash-derived
* client-provided filenames are never used for server storage
* skin hashes are lowercase `0x`-prefixed hex strings

Dedicated server skin storage:

```text
server-save/player_assets/skins/<skin_hash>.png
```

Client downloaded skin cache:

```text
arvane_home/cache/skins/<skin_hash>.png
```

Local user-owned skin files:

```text
arvane_home/skins/<skin_hash>.png
```

Server and client skin writes should use temporary files before renaming into place.

---

## Server Profile Storage

Server profiles are stored in the save directory.

Recommended layout:

```text
arvane_home/
  saves/
    server-save-1/
      players/
        0x8b3f7a1d4c9e2f00151b0db7f49c4cb2d7e1c69a0bb47f0fd7a66d6b89d5a421.json
      player_assets/
        skins/
          <skin_hash>.png
```

Example server player profile:

```json
{
  "version": 1,
  "profile_id": "0x8b3f7a1d4c9e2f00151b0db7f49c4cb2d7e1c69a0bb47f0fd7a66d6b89d5a421",
  "public_key": "0x2e5c2f90884cb17f47b22cb4ab64efb7ccfa1343e91e8d1923a55db97a187944",
  "display_name": "Hippo",
  "selected_skin_hash": "0x13a465fc6616da8d2afacaf00a8276aa841662820f9337de127f815ee71cc55d",
  "selected_skin_slim_model": false,

  "last_world": "arvane:overworld",
  "last_position": [12.5, 70.0, -44.5],
  "last_rotation": [90.0, 0.0],

  "spawn_world": "arvane:overworld",
  "spawn_position": [0.5, 70.0, 0.5],

  "game_mode": "survival",
  "permission_level": 0,

  "inventory": {},
  "stats": {},
  "settings": {}
}
```

The server profile stores the public key, never the private key.

`selected_skin_hash` is the custom skin currently selected for this player on
that server. Empty string means no custom skin. `selected_skin_slim_model`
selects the slim player geometry for that profile; it does not change the PNG
hash, storage path, or animation asset.

---

## Player Profile Runtime Model

`arv_server` owns persistent profiles.

Example:

```cpp
struct PlayerProfile {
    std::string profile_id;

    arv::crypto::Ed25519PublicKey public_key;

    std::string display_name;
    std::string selected_skin_hash;
    bool selected_skin_slim_model;

    std::string last_world;
    glm::dvec3 last_position;
    glm::vec2 last_rotation;

    std::string spawn_world;
    glm::dvec3 spawn_position;

    GameMode game_mode;
    PermissionLevel permission_level;

    PlayerInventoryData inventory;
    PlayerStats stats;
    PlayerSettings settings;

    bool dirty = false;
};
```

`profile_id` is a stable string beginning with `0x`.

---

## Player Session Model

`arv_server` owns player sessions.

Example:

```cpp
struct PlayerSession {
    PlayerSessionID session_id;
    ConnectionID connection_id;

    std::string profile_id;

    bool authenticated = false;
    bool pending_auth = true;

    WorldID active_world {};
    entt::entity active_entity { entt::null };

    std::optional<AuthChallenge> challenge;
};
```

The session is runtime-only and is not saved.

---

## World/ECS Integration

`arv_world` does not own player profiles.

The world only needs to know that an entity is controlled by a player.

Example component:

```cpp
struct PlayerControlled {
    std::string profile_id;
    PlayerSessionID session_id;
};
```

The world may use this for ownership checks, replication association, and debugging.

The authoritative profile data remains in `arv_server`.

---

## Authentication Flow

Authentication uses challenge-response signing.

```text
1. Client connects.
2. Client sends public key and display name.
3. Server derives profile_id from public key.
4. Server loads or creates profile.
5. Server creates random auth challenge.
6. Server sends challenge to client.
7. Client signs deterministic challenge payload with private key.
8. Client sends signature.
9. Server verifies signature with public key.
10. Server marks session authenticated.
11. Server spawns or restores player entity.
```

The private key is never sent to the server.

### First Join

Because there is no central account server, identity is per server.

Recommended policy:

```text
first valid connection for a new profile_id creates the server profile
```

First-join flow:

```text
client connects
client sends public key and display name
server derives profile_id from public key
server checks if profile exists
profile does not exist
server creates profile
server begins challenge authentication
```

The server does not trust a client-supplied profile id. The server derives profile id from the supplied public key.

### Existing Profile Login

Existing profile flow:

```text
client connects
client sends public key and display name
server derives profile_id from public key
server finds matching profile
server verifies stored public key matches supplied public key
server begins challenge authentication
```

If the profile file exists but the supplied public key does not match the stored public key, login fails.

---

## Challenge Requirements

The server challenge must be:

* random
* single-use
* short-lived
* bound to the connection/session
* discarded after success or failure
* invalid after timeout
* invalid after disconnect

Recommended challenge data:

```cpp
struct AuthChallenge {
    PlayerSessionID session_id;
    std::array<std::byte, 32> nonce;
    u64 issued_server_tick;
    u64 expires_server_tick;
};
```

The nonce must be generated by `arv_crypto`.

Do not use `std::rand`.

---

## Signed Payload

The client does not sign only the raw nonce. The signed message includes enough context to prevent replay across sessions, servers, or protocol versions.

Recommended signed payload fields:

```text
protocol_magic
protocol_version
server_instance_id
session_id
profile_id
public_key
nonce
expires_server_tick
```

Example conceptual payload:

```text
ARVANE_AUTH_V1
protocol_version=1
server_instance_id=<server uuid>
session_id=<session id>
profile_id=0x...
public_key=0x...
nonce=0x...
expires_server_tick=123456
```

The exact wire encoding is defined in `arv_protocol`.

The server and client must build the signed payload deterministically. Do not sign JSON unless the serialization is canonical. Prefer compact binary protocol encoding.

---

## Replay Protection

A valid signature must not be reusable.

The server rejects:

* unknown challenge
* expired challenge
* challenge for another session
* reused challenge
* challenge from another server instance
* signature for a different profile id
* signature for a different public key
* signature with mismatched protocol version

After a login attempt completes, the challenge is erased regardless of success or failure.

---

## Join Flow

After successful authentication:

```text
server marks session authenticated
server selects target world
server loads or creates target world if needed
server creates player entity
server attaches PlayerControlled component
server restores profile-derived state
server sends initial world/player packets
server sends skin rules/profile metadata packets
server begins chunk streaming
```

The target world is usually:

```text
profile.last_world
```

If missing or invalid, use the configured default world.

The target position is usually:

```text
profile.last_position
```

If missing or invalid, use:

```text
profile.spawn_position
```

If that is missing, use the world spawn.

---

## Disconnect Flow

On disconnect:

```text
server finds authenticated session
server snapshots entity state into PlayerProfile
server marks profile dirty
server saves profile
server destroys or suspends player entity
server removes session
```

State copied from entity to profile may include:

* current world
* position
* rotation
* inventory
* selected hotbar slot
* game mode
* health, if treated as persistent
* other persistent gameplay data

Do not copy transient physics/input state into the profile.

Avoid saving:

* current key state
* current jump flag
* collision flags
* animation phase
* camera bob
* prediction buffers
* interpolation state

---

## Respawn Flow

Respawn preserves the profile and session.

```text
player dies
server determines spawn point from profile/world rules
server destroys or resets old entity
server creates new player entity
server reattaches PlayerControlled
server keeps same profile_id
server keeps same session_id
```

The profile identity does not change on death.

---

## World Transfer Flow

Dimension/world transfer moves the player profile to a new entity in another world.

```text
snapshot relevant entity state
remove entity from old world
ensure destination world is loaded
create entity in destination world
restore transfer state
update session.active_world
update session.active_entity
update profile.last_world
update profile.last_position
send world-transfer packet to client
begin chunk streaming around new position
```

The entity does not need to persist across worlds. The profile does.

---

## Display Name Rules

Display names are user-facing metadata.

Rules:

* mutable
* non-authoritative
* not used as save filename
* not used as map key
* replicated only for UI
* sanitized before rendering
* length-limited
* valid UTF-8
* not globally unique

The protocol identifies players by profile id, runtime player id, or runtime entity id, not display name.

UI, logs, and debug tools may disambiguate duplicate names with a short profile id suffix:

```text
Hippo [0x8b3f7a1d]
```

---

## Permission Model

Permissions are stored on the server profile, not the client.

Example:

```cpp
enum class PermissionLevel : u8 {
    Default = 0,
    Moderator = 1,
    Admin = 2,
    Owner = 3,
};
```

The client may display permission status, but the server is authoritative.

Command execution, debug actions, skin-resolution permissions, block editing, and admin tools must check server-side permission state.

---

## Player Skin Server Rules

Dedicated servers may define skin policy in server config.

Example:

```yaml
skins:
  enabled: true
  allow_uploads: true

  resolutions:
    - size: 64
      enabled: true
      max_upload_bytes: 32768
      max_decoded_bytes: 16384

    - size: 128
      enabled: false
      permission: "example.perm.128skin"

    - size: 256
      enabled: false

    - size: 512
      enabled: false

  uploads:
    max_chunk_bytes: 32768
    chunk_threshold_bytes: 1048576
    max_active_per_connection: 1
    max_active_global: 32
    timeout_seconds: 15
    idle_timeout_seconds: 5

  downloads:
    require_advertised_hash: true
    max_batch_size: 16

  storage:
    max_total_skin_bytes: 1073741824
    max_skins_per_profile: 16
```

`skins.enabled` disables the entire custom skin system when false.

`skins.allow_uploads` controls whether clients may submit new skin data. Existing selected server skins may still be advertised and requested when `skins.enabled` is true.

Resolution entries define which square skin sizes are accepted.

Default `max_upload_bytes`:

```text
64   => 32 KiB
128  => 64 KiB
256  => 256 KiB
512  => 1 MiB
1024 => 4 MiB
2048 => 16 MiB
```

Default `max_decoded_bytes`:

```text
size * size * 4
```

By default, dedicated servers enable only 64x64 skins.

Integrated singleplayer has no effective local skin restriction and uses the first skin entry in the active profile’s skin list.

---

## Skin Transfer Contract

Profile metadata replication sends only:

```text
player_id
profile_id
display_name
SkinDescriptor
```

Image bytes are transferred on demand with skin request/upload packets.

Upload hardening:

* uploads require an authenticated session
* uploads require configured skin support
* uploads require enabled resolution
* width and height must match the selected resolution
* upload byte count must not exceed the resolution limit
* decoded byte count must not exceed the resolution limit
* only one active upload per connection by default
* upload ids are server-issued and session-owned
* upload ids expire after configured total or idle timeout
* full uploads must provide exactly the negotiated byte count
* chunked uploads must use non-empty chunks no larger than the configured chunk size
* chunked uploads may not overflow the negotiated total
* final validation decodes PNG, checks dimensions, checks RGBA8 byte count, computes authoritative hash from compressed bytes, and updates the selected skin hash and slim-model flag only after storage succeeds

Download hardening:

* downloaded skins are request-scoped
* clients may request only hashes advertised by the server unless config explicitly allows arbitrary lookup
* clients validate request id, hash, byte count, PNG decode, and dimensions before writing cache files
* cache writes use hash-derived filenames

Server and client skin writes use temporary files before renaming into place.

Future cleanup should add garbage collection for unreferenced server skin blobs, such as skins not selected by any profile, skins not recently used, or skins older than a server-defined threshold.

---

## Protocol Packets

Packet names are illustrative. Exact packet naming should follow the protocol documentation style.

### Clientbound Auth Challenge

```cpp
struct ClientboundAuthChallenge {
    PlayerSessionID session_id;
    std::array<std::byte, 32> nonce;
    u64 expires_server_tick;
    ServerInstanceID server_instance_id;
};
```

### Serverbound Auth Begin

```cpp
struct ServerboundAuthBegin {
    arv::crypto::Ed25519PublicKey public_key;
    std::string display_name;
};
```

The server derives:

```text
profile_id = 0x + lowercase_hex(hash(public_key_bytes))
```

The client may include its derived profile id for diagnostics, but the server must not trust it.

### Serverbound Auth Response

```cpp
struct ServerboundAuthResponse {
    arv::crypto::Ed25519Signature signature;
};
```

The signature is over the deterministic signed auth payload.

### Clientbound Auth Result

```cpp
struct ClientboundAuthResult {
    bool accepted;
    std::string profile_id;
    std::string reason_if_rejected;
};
```

Avoid leaking detailed security-sensitive rejection reasons to untrusted clients. Logs can contain more detail than network responses.

### Skin Descriptor

```cpp
enum class SkinImageFormat : u8 {
    Png = 0,
};

struct SkinDescriptor {
    std::string skin_hash; // empty means no custom skin
    SkinImageFormat format;
    u16 width;
    u16 height;
    bool slim_model;
};
```

### Clientbound Skin Rules

```cpp
struct ClientboundSkinRules {
    bool enabled;
    bool allow_uploads;
    u16 max_res;
    SkinDescriptor selected_skin;
};
```

Sent after successful authentication.

### Clientbound Player Profile Info

```cpp
struct ClientboundPlayerProfileInfo {
    std::string profile_id;
    std::string display_name;
    SkinDescriptor skin;
};
```

Batch variant:

```cpp
struct ClientboundPlayerProfileInfoBatch {
    std::vector<PlayerProfileInfo> profiles;
};
```

### Skin Requests and Skin Data

```cpp
struct SkinRequestEntry {
    u32 request_id;
    std::string skin_hash;
};

struct ServerboundSkinRequest {
    u32 request_id;
    std::string skin_hash;
};

struct ServerboundSkinRequestBatch {
    std::vector<SkinRequestEntry> requests;
};

struct ClientboundSkinRequestDeny {
    u32 request_id;
    std::string skin_hash;
    SkinRequestDenyReason reason;
};

struct ClientboundSkinData {
    u32 request_id;
    std::string skin_hash;
    SkinImageFormat format;
    u16 width;
    u16 height;
    std::vector<u8> compressed_image_bytes;
};

struct ClientboundSkinDataBeginChunked {
    u32 request_id;
    std::string skin_hash;
    SkinImageFormat format;
    u16 width;
    u16 height;
    u32 total_bytes;
};

struct ClientboundSkinDataChunk {
    u32 request_id;
    std::vector<u8> chunk;
};

struct ClientboundSkinDataEndChunked {
    u32 request_id;
};
```

### Skin Uploads

```cpp
enum class SkinUploadMode : u8 {
    Full = 0,
    Chunked = 1,
};

struct ServerboundSkinUploadBegin {
    SkinImageFormat format;
    std::string client_skin_hash_hint; // may be empty; server does not trust it
    u16 width;
    u16 height;
    bool slim_model;
    u32 total_upload_bytes;
};

struct ClientboundSkinUploadAccept {
    u32 upload_id;
    SkinUploadMode upload_mode;
    u32 max_chunk_bytes;
};

struct ClientboundSkinUploadDeny {
    SkinUploadDenyReason reason;
};

struct ServerboundSkinUploadData {
    u32 upload_id;
    std::vector<u8> compressed_image_bytes;
};

struct ServerboundSkinUploadChunk {
    u32 upload_id;
    std::vector<u8> image_chunk_bytes;
};

struct ServerboundSkinUploadEnd {
    u32 upload_id;
};

struct ClientboundSkinUploadComplete {
    u32 upload_id;
    SkinDescriptor skin;
};

struct ClientboundSkinUploadFailed {
    u32 upload_id;
    SkinUploadDenyReason reason;
};
```

Deny reasons:

```cpp
enum class SkinRequestDenyReason : u8 {
    SkinsDisabled,
    InvalidHash,
    NotFound,
    NotAdvertisedToClient,
    RateLimited,
    TooLarge,
    InternalError,
};

enum class SkinUploadDenyReason : u8 {
    SkinsDisabled,
    UploadsDisabled,
    NotAuthenticated,
    InvalidFormat,
    InvalidResolution,
    ResolutionNotAllowed,
    UploadTooLarge,
    RateLimited,
    UploadAlreadyPending,
    ServerStorageFull,
    DecodeFailed,
    HashMismatch,
    InternalError,
};
```

---

## Integrated Server Behavior

Singleplayer uses the same profile/session model where practical.

Recommended behavior:

```text
local client loads active profile from profiles.json
integrated server accepts local profile
integrated server creates/loads matching profile in save directory
same profile/session/entity model is used
```

The integrated server may skip the network challenge when running in-process, but it still uses the same profile id derivation, profile storage, and selected skin field.

---

## Dedicated Server Behavior

Dedicated servers authenticate remote clients with challenge-response.

No remote client becomes an active player session until signature verification succeeds.

Dedicated servers may support:

```yaml
auth:
  allow_first_join_profile_creation: true
  require_known_profiles: false
```

If `require_known_profiles` is true, the server rejects unknown profile ids unless a profile file or allowlist entry already exists.

---

## Optional Allowlist

Dedicated servers may support a public-key allowlist.

Example:

```text
server-save-1/
  auth/
    allowlist.json
```

Example:

```json
{
  "allowed_profiles": [
    {
      "profile_id": "0x8b3f7a1d4c9e2f00151b0db7f49c4cb2d7e1c69a0bb47f0fd7a66d6b89d5a421",
      "public_key": "0x2e5c2f90884cb17f47b22cb4ab64efb7ccfa1343e91e8d1923a55db97a187944",
      "display_name_hint": "Hippo"
    }
  ]
}
```

This is optional and should not be required for local or open servers.

---

## Logging

Logs should include profile ids with the `0x` prefix.

Examples:

```text
auth: created profile 0x8b3f7a1d... display_name=Hippo
auth: accepted profile 0x8b3f7a1d... connection=42
auth: rejected profile 0x8b3f7a1d... reason=signature_verify_failed
skin: selected profile=0x8b3f7a1d... skin=0x13a465fc...
skin: upload rejected profile=0x8b3f7a1d... reason=UploadTooLarge
```

For readability, logs may shorten ids after the prefix:

```text
0x8b3f7a1d
```

Save files and protocol payloads should use full ids.

---

## Error Handling

Authentication fails cleanly for:

* malformed public key
* malformed signature
* malformed profile id
* profile id/public key mismatch
* unknown profile when unknown profiles are disabled
* expired challenge
* reused challenge
* failed signature verification
* unsupported protocol version
* invalid display name

Failure should close or reset the pending login state.

Do not create a player entity before authentication succeeds.

Skin operations fail cleanly for:

* unauthenticated session
* disabled skin system
* uploads disabled
* invalid hash
* invalid format
* invalid resolution
* resolution not allowed by permission
* upload too large
* decoded image too large
* decode failure
* upload timeout
* stale upload id
* storage quota failure
* request for unadvertised skin hash

Failures should not crash the client or server. Use the default skin as the rendering fallback.

---

## Security Constraints

Do:

* use a vetted crypto library through `arv_crypto`
* use Ed25519 for signatures
* generate cryptographically secure random nonces
* store only public keys on the server
* derive profile id from public key server-side
* bind signatures to challenge/session/server/protocol context
* reject replayed or expired challenges
* keep third-party crypto usage inside `arv_crypto`
* treat skin data as untrusted binary input
* store skins by server-computed hash only
* validate received skin bytes before caching or uploading to the GPU

Do not:

* invent a custom signature algorithm
* use display name as identity
* trust client-supplied profile id
* trust client-supplied skin hash as authoritative
* send private keys over the network
* store private keys in server saves
* use runtime IDs in saved profile data
* sign ambiguous or non-canonical payloads
* identify permissions by display name
* write client-provided skin filenames
* delete local skin/cache files during GPU atlas eviction

---

## Out of Scope

The following are intentionally not part of this design:

* central account server
* global username reservation
* email/password accounts
* OAuth/platform login
* account recovery
* cloud saves
* friends list
* matchmaking
* global bans
* global profile inventory
* economy ownership verification
* private-key encryption with OS keychain integration
* skin content moderation
* skin garbage collection beyond documented storage limits/TODOs

These may be added later through identity-provider, profile-management, or server-moderation extensions.

---

## Future Extension: Identity Provider Interface

The current identity provider is local public-key authentication. Future providers may authenticate through a central service, passkeys, platform accounts, or wallet signatures.

Possible future interface:

```cpp
class IdentityProvider {
public:
    virtual ~IdentityProvider() = default;

    virtual AuthBeginResult begin_auth(
        const ConnectionID& connection,
        const arv::crypto::Ed25519PublicKey& public_key,
        std::string_view display_name
    ) = 0;

    virtual AuthVerifyResult verify_auth_response(
        const ConnectionID& connection,
        const arv::crypto::Ed25519Signature& signature
    ) = 0;
};
```

Initial implementation:

```text
LocalPublicKeyIdentityProvider
```

Future implementation examples:

```text
CentralAccountIdentityProvider
PasskeyIdentityProvider
PlatformIdentityProvider
```

The rest of the server should not care which provider is used.

---

## Testing Requirements

`arv_crypto` should have tests for:

* Ed25519 key generation
* sign/verify success
* sign/verify failure with wrong key
* sign/verify failure with modified payload
* hash stability
* hex encoding
* `0x`-prefixed profile id derivation
* lowercase output
* malformed hex rejection if parsing is implemented

Local profile tests:

* `profiles.json` load/save
* multiple profiles
* first-profile active fallback
* private key path resolution
* local skin path resolution
* cache skin path resolution
* display name changes do not change profile id
* skin list ordering
* best skin selection by max resolution

`arv_server` tests:

* first-join profile creation
* existing profile login
* duplicate display names
* public key mismatch rejection
* expired challenge rejection
* replayed challenge rejection
* profile save/load roundtrip
* selected skin hash save/load
* disconnect snapshots entity state into profile

Skin tests:

* skin hash formatting
* default max upload bytes by resolution
* default max decoded bytes
* upload denial cases
* upload completion result
* upload failure result
* request denial for invalid/unavailable/unadvertised hash
* client cache path derivation
* server skin storage path derivation

---

## Final Architecture Rule

Use this identity stack:

```text
private key -> proves ownership
public key  -> identifies cryptographic identity
hash(public_key) -> stable PlayerProfileID
display name -> mutable UI label
session -> current connection
entity -> current in-world body
profile -> persistent player state
selected_skin_hash -> profile-selected custom skin on a server
selected_skin_slim_model -> profile-selected slim player geometry flag
```

The profile id must always be represented as:

```text
0x<lowercase hex hash>
```

Display name must never be used as authoritative identity.
