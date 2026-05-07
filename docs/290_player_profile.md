# Player Profiles and Public-Key Authentication

## Intent

Arvane uses player profiles to represent persistent player identity and cross-world player state.

Authentication is based on a public/private key pair. The client owns the private key. The server stores or learns the public key. During login, the server proves that the connecting client controls the matching private key by issuing a challenge that the client must sign.

Arvane does **not** require a central account server at this stage. Player identity is per-installation or per-server. Global account identity, global username reservation, and cross-server profile portability are out of scope for now.

This design supports:

* dedicated servers
* integrated singleplayer servers
* offline/local profiles
* server-authoritative gameplay
* duplicate display names
* future central identity integration without changing the world/entity model

---

## Module Placement

A new CMake submodule should be created:

```text
arv_crypto
```

This module owns the engine-facing cryptography wrapper.

Do **not** place cryptography directly in:

```text
arv_server
arv_client
arv_world
arv_protocol
arv_core
```

Those modules may depend on `arv_crypto`, but they should not include third-party crypto headers directly.

Recommended structure:

```text
src/
  arv_crypto/
    CMakeLists.txt
    include/
      arv/crypto/crypto_init.hpp
      arv/crypto/random.hpp
      arv/crypto/ed25519.hpp
      arv/crypto/hash.hpp
      arv/crypto/hex.hpp
    src/
      crypto_init.cpp
      random.cpp
      ed25519.cpp
      hash.cpp
      hex.cpp
```

Recommended dependency:

```text
libsodium
```

`arv_crypto` should wrap the dependency behind Arvane-owned types and functions.

The rest of the engine should use only:

```cpp
#include <arv/crypto/...>
```

and should not include:

```cpp
#include <sodium.h>
```

outside `arv_crypto`.

---

## Core Concepts

Arvane separates three concepts:

```text
PlayerProfile
PlayerSession
PlayerEntity
```

### PlayerProfile

Persistent player-owned state.

Examples:

* stable profile id
* public authentication key
* display name
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

Transient runtime connection state.

Examples:

* connection id
* session id
* authenticated state
* active world
* active entity
* pending login/challenge state

The session exists only while the player is connected.

### PlayerEntity

The active in-world simulation body.

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

Arvane should not use username as identity.

The authoritative player identity is:

```text
PlayerProfileID = hash(public_key)
```

The display name is mutable and non-authoritative.

This allows duplicate display names:

```text
Hippo
Hippo
Hippo
```

Internally, those players are distinct because their public keys are distinct.

---

## Profile ID Format

All profile IDs derived from public keys must be represented as lowercase hexadecimal strings prefixed with:

```text
0x
```

Example:

```text
0x8b3f7a1d4c9e2f00151b0db7f49c4cb2d7e1c69a0bb47f0fd7a66d6b89d5a421
```

The `0x` prefix is required for easy identification in logs, save files, debug tools, and protocol traces.

### ID Rules

Profile IDs:

* are derived from the public key
* are stable as long as the key pair does not change
* are not derived from username
* are serialized as strings
* use lowercase hex
* include the `0x` prefix
* should not contain separators
* should not depend on runtime IDs

Recommended derivation:

```text
profile_id = "0x" + hex(hash(public_key_bytes))
```

The hash algorithm should be selected and wrapped inside `arv_crypto`.

A reasonable default is a 256-bit cryptographic hash.

---

## Public and Private Keys

Each player identity has:

```text
private key
public key
```

The private key is secret and remains on the client machine.

The public key can be stored by servers and shared over the network.

Recommended signature algorithm:

```text
Ed25519
```

Recommended key sizes:

```text
public key:  32 bytes
private key: 64 bytes
signature:   64 bytes
```

These sizes should be exposed through constants in `arv_crypto`, not hardcoded throughout the engine.

Example:

```cpp
namespace arv::crypto {

inline constexpr usize ed25519_public_key_size = 32;
inline constexpr usize ed25519_private_key_size = 64;
inline constexpr usize ed25519_signature_size = 64;

}
```

---

## `arv_crypto` API Shape

The engine should expose strongly typed wrappers.

Example:

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

}
```

Hashing:

```cpp
namespace arv::crypto {

struct Hash256 {
    std::array<std::byte, 32> bytes;
};

Hash256 hash_256(std::span<const std::byte> data);

std::string to_lower_hex(std::span<const std::byte> data);
std::string to_prefixed_lower_hex(std::span<const std::byte> data);

}
```

Profile ID helper:

```cpp
namespace arv::crypto {

std::string derive_profile_id_from_public_key(
    const Ed25519PublicKey& public_key
);

}
```

This function must return a string beginning with `0x`.

---

## Crypto Initialization

`arv_crypto` should expose explicit initialization:

```cpp
namespace arv::crypto {

bool initialize_crypto();

}
```

The application entry points should call this during startup before generating keys, signing, verifying, hashing, or generating random bytes.

Recommended callers:

```text
arv_desktop
arv_dedicated
tests using arv_crypto
```

Do not scatter third-party crypto initialization throughout gameplay or networking code.

---

## Client Identity Storage

The client should store local identity material under:

```text
arvane_home/identity/
```

Recommended layout:

```text
arvane_home/
  identity/
    local_identity.json
    local_private_key.bin
```

Example `local_identity.json`:

```json
{
  "version": 1,
  "profile_id": "0x8b3f7a1d4c9e2f00151b0db7f49c4cb2d7e1c69a0bb47f0fd7a66d6b89d5a421",
  "display_name": "Hippo",
  "public_key": "0x2e5c2f90884cb17f47b22cb4ab64efb7ccfa1343e91e8d1923a55db97a187944",
  "private_key_file": "local_private_key.bin"
}
```

The private key should be stored separately from the public metadata file.

The binary private key file contains the raw private key bytes.

For now, private-key-at-rest encryption is optional. The engine should not pretend the key is protected if it is only stored as a local file. A future account/security pass may add OS keychain integration.

---

## Local Identity Creation

If no local identity exists, the client should create one automatically.

Flow:

```text
client startup
identity file missing
generate Ed25519 key pair
derive profile_id from public key
write local_identity.json
write local_private_key.bin
use generated identity for local/integrated play
```

The user may later change display name without changing identity.

Changing the key pair creates a new identity.

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
```

Example:

```json
{
  "version": 1,
  "profile_id": "0x8b3f7a1d4c9e2f00151b0db7f49c4cb2d7e1c69a0bb47f0fd7a66d6b89d5a421",
  "public_key": "0x2e5c2f90884cb17f47b22cb4ab64efb7ccfa1343e91e8d1923a55db97a187944",
  "display_name": "Hippo",

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

---

## First-Join Policy

Because there is no central account server, identity is per server.

Recommended policy:

```text
first valid connection for a new profile_id creates the profile
```

Since `profile_id` is derived from the public key, the server can independently verify that the claimed profile id matches the supplied public key.

First join flow:

```text
client connects
client sends public key and display name
server derives profile_id from public key
server checks if profile exists
profile does not exist
server creates profile
server begins challenge authentication
```

The server should not trust a client-supplied profile id without deriving it independently from the supplied public key.

---

## Existing Profile Login Policy

Existing profile flow:

```text
client connects
client sends public key and display name
server derives profile_id from public key
server finds matching profile
server verifies stored public key matches supplied public key
server begins challenge authentication
```

If the profile file exists but the supplied public key does not match the stored public key, login must fail.

This prevents a client from claiming another existing profile id.

---

## Authentication Flow

Authentication uses challenge-response signing.

```text
1. Client connects
2. Client sends public key and display name
3. Server derives profile_id = hash(public_key)
4. Server loads or creates profile
5. Server creates random auth challenge
6. Server sends challenge to client
7. Client signs challenge payload with private key
8. Client sends signature
9. Server verifies signature with public key
10. Server marks session authenticated
11. Server spawns or restores player entity
```

The private key is never sent to the server.

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

The client should not sign only the raw nonce. The signed message should include enough context to prevent replay across sessions, servers, or protocol versions.

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

The exact wire encoding should be defined in `arv_protocol`.

The server and client must build the signed payload deterministically. Do not sign JSON unless the serialization is canonical. Prefer a compact binary protocol encoding.

---

## Replay Protection

A valid signature must not be reusable.

The server must reject:

* unknown challenge
* expired challenge
* challenge for another session
* reused challenge
* challenge from another server instance
* signature for a different profile id
* signature for a different public key
* signature with mismatched protocol version

After a login attempt completes, the challenge should be erased regardless of success or failure.

---

## Player Session Model

`arv_server` should own player sessions.

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

The session is runtime-only and should not be saved.

---

## Player Profile Model

`arv_server` should own persistent profiles.

Example:

```cpp
struct PlayerProfile {
    std::string profile_id;

    arv::crypto::Ed25519PublicKey public_key;

    std::string display_name;

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

## World/ECS Integration

`arv_world` must not own player profiles.

The world should only know that an entity is controlled by a player.

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

Respawn should preserve the profile and session.

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

Dimension/world transfer should move the player profile to a new entity in another world.

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

## Duplicate Display Names

Arvane should allow duplicate display names.

Display name is not identity.

This is valid:

```text
profile_id:    0x1111...
display_name:  Hippo

profile_id:    0x2222...
display_name:  Hippo
```

UI, logs, and debug tools may disambiguate using a short profile id suffix:

```text
Hippo [0x8b3f7a1d]
```

Do not require display names to be globally unique.

---

## Display Name Rules

Display names should be treated as user-facing metadata.

Recommended rules:

* mutable
* non-authoritative
* not used as save filename
* not used as map key
* replicated only for UI
* sanitized before rendering
* length-limited
* valid UTF-8

The protocol should identify players by profile id or runtime entity id, not display name.

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

Command execution, debug actions, block editing, and admin tools must check server-side permission state.

---

## Integrated Server Behavior

Singleplayer should use the same profile/session/auth model where practical.

Recommended behavior:

```text
local client loads local identity
integrated server accepts local identity
integrated server creates/loads matching profile in save directory
same profile/session/entity model is used
```

The integrated server may skip the network challenge if running in-process, but it should still use the same profile id derivation and profile storage format.

This avoids two separate player-save systems.

---

## Dedicated Server Behavior

Dedicated servers should always authenticate remote clients with challenge-response.

No remote client should become an active player session until signature verification succeeds.

Dedicated servers may support a configuration policy:

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
profile_id = hash(public_key)
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

Avoid leaking detailed security-sensitive rejection reasons to untrusted clients. Logs can contain more detail than the network response.

---

## Logging

Logs should include profile IDs with the `0x` prefix.

Examples:

```text
auth: created profile 0x8b3f7a1d... display_name=Hippo
auth: accepted profile 0x8b3f7a1d... connection=42
auth: rejected profile 0x8b3f7a1d... reason=signature_verify_failed
```

For readability, logs may shorten profile IDs after the prefix:

```text
0x8b3f7a1d
```

but save files and protocol payloads should use the full profile id.

---

## Error Handling

Authentication should fail cleanly for:

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

---

## Testing Requirements

`arv_crypto` should have unit tests for:

* Ed25519 key generation
* sign/verify success
* sign/verify failure with wrong key
* sign/verify failure with modified payload
* hash stability
* hex encoding
* `0x`-prefixed profile id derivation
* lowercase output
* malformed hex rejection if parsing is implemented

`arv_server` should have tests for:

* first-join profile creation
* existing profile login
* duplicate display names
* public key mismatch rejection
* expired challenge rejection
* replayed challenge rejection
* profile save/load roundtrip
* disconnect snapshots entity state into profile
* username/display name changes do not change profile id

---

## Security Constraints

Do:

* use a vetted crypto library
* use Ed25519 for signatures
* generate cryptographically secure random nonces
* store only public keys on the server
* derive profile id from public key server-side
* bind signatures to challenge/session/server/protocol context
* reject replayed or expired challenges
* keep third-party crypto usage inside `arv_crypto`

Do not:

* invent a custom signature algorithm
* use username as identity
* trust client-supplied profile id
* send private keys over the network
* store private keys in server saves
* use runtime IDs in saved profile data
* sign ambiguous or non-canonical payloads
* identify permissions by display name

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

These may be added later through an identity provider abstraction.

---

## Future Extension: Identity Provider Interface

Even though Arvane is not using a central account server now, code should leave room for one later.

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

Future implementation:

```text
CentralAccountIdentityProvider
```

The rest of the server should not care which provider is used.

---

## Recommended Implementation Order

1. Create `arv_crypto` CMake submodule.
2. Add wrapped Ed25519 key/sign/verify APIs.
3. Add hash and `0x`-prefixed hex helpers.
4. Add profile id derivation from public key.
5. Add client local identity generation/loading.
6. Add server player profile save/load.
7. Add auth packets to `arv_protocol`.
8. Add server challenge-response login.
9. Add player session/profile/entity separation.
10. Add tests for crypto and profile authentication.
11. Add integrated-server path using the same profile id format.

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
```

The profile id must always be represented as:

```text
0x<lowercase hex hash>
```

Username/display name must never be used as the authoritative identity.

---

## Local Profile Storage

Local installations store profiles at the `arvane_home` root, not inside the old
`identity/` directory:

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

`profiles.json` is a JSON array. The first entry is the active local profile
until a profile-selection UI exists. Each entry stores metadata, public key, the
private key filename, and the profile-owned local skin list. Private key bytes
must never be written into `profiles.json`; they are loaded from
`keys/<private_key_file>`.

Local skin files in `skins/` are user-owned. Server-downloaded skin files are
cached only in `cache/skins/` and must not be copied into `skins/` unless the
user explicitly imports them as a local skin.

When `profiles.json` is missing, the client may migrate the legacy
`identity/local_identity.json` plus `identity/local_private_key.bin` into the
new layout. Migration writes the new files and leaves the old files in place.

All profile ids, public keys, and skin hashes are lowercase `0x`-prefixed
hexadecimal strings.

---

## Player Skins

The default fallback skin is:

```text
arvane_home/assets/textures/default_skin/default.png
arvane:texture/default_skin/default
```

The default skin is used when a profile has no applicable local skin, when
server skins are disabled, while remote skins download, or when validation
fails. It is not uploaded as a custom skin unless the user imports/selects it as
a normal local skin.

Skin hashes are content hashes of the original accepted PNG bytes:

```text
skin_hash = "0x" + lowercase_hex(hash(original_png_bytes))
```

The client may compute hashes for cache lookup and upload hints. The server is
authoritative: it computes the final hash from uploaded bytes, stores by that
hash only, and never trusts client filenames or client-supplied hashes.

Dedicated servers persist a per-profile `selected_skin_hash`. Empty string means
no custom skin. Uploaded skin files live under:

```text
server-save/player_assets/skins/<skin_hash>.png
```

Profile metadata replication sends only:

```text
player_id
profile_id
display_name
SkinDescriptor
```

Image bytes are transferred on demand with skin request/upload packets.
