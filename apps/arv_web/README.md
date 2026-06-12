# arv_web

`arv_web` is the V1 multiplayer-only browser client for Arvane. It is built with Emscripten, runs in a WebGL2-capable browser, and connects to the native dedicated server over WebSocket.

## Prerequisites

- Emscripten SDK with `emcmake`, `emcc`, and `em++` on `PATH`.
- CMake 3.19 or newer.
- A browser with WebGL2 enabled.
- A native Arvane build for `arv_dedicated` while the web client is developed.

On this workstation, the Emscripten environment can be loaded with:

```sh
source /home/hippo/emsdk/emsdk_env.sh
```

## Build

Configure the web build with Emscripten:

```sh
emcmake cmake -S . -B build-web -DARVANE_BUILD_WEB=ON
cmake --build build-web --target arv_web
```

The V1 target writes browser artifacts to:

```txt
build-web/apps/arv_web/dist/
```

The manual verification helper runs the same build steps and prints the browser checklist:

```sh
apps/arv_web/manual_verify.sh
```

Use `--check` to verify prerequisites only, or `--serve` to serve the generated `dist` directory after a successful build.

Current Emscripten target flags are:

```txt
-lidbfs.js
-sENVIRONMENT=web
-sUSE_WEBGL2=1
-sMIN_WEBGL_VERSION=2
-sMAX_WEBGL_VERSION=2
-sALLOW_MEMORY_GROWTH=1
-sINITIAL_MEMORY=67108864
-sSTACK_SIZE=1048576
-sFILESYSTEM=1
-sFORCE_FILESYSTEM=1
-sASSERTIONS=1
-sEXPORTED_FUNCTIONS=['_main','_arv_web_frame','_arv_web_connect','_arv_web_disconnect','_arv_web_crypto_self_test']
-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']
```

## Serve

Serve the generated `dist` directory with any static file server:

```sh
python3 -m http.server 8080 --directory build-web/apps/arv_web/dist
```

Then open:

```txt
http://localhost:8080/
```

## Server Config

The dedicated server can optionally run a WebSocket listener for browser clients:

```yaml
network:
  web:
    enabled: false
    bind_address: 0.0.0.0
    port: 42070
    path: /arvane
    max_frame_bytes: 1048576
    max_pending_bytes: 4194304
```

With the defaults above and `enabled: true`, the local browser URL is:

```txt
ws://localhost:42070/arvane
```

The native GameNetworkingSockets listener remains enabled for native clients. When `network.web.enabled` is true, the dedicated server starts both listeners and routes browser packets through the same server session path. Browser frames must be masked binary WebSocket frames; text, unmasked, malformed, oversized, and wrong-path clients are rejected before gameplay state.

## Cache

Browser persistence uses a browser-backed `arvane_home` layout:

```txt
/arvane_home/
  config/
  cache/server-packs/
  logs/
```

The shell mounts `/arvane_home` with IDBFS before `main()` runs, syncs browser storage into memory, creates the V1 cache/config/log directories, and blocks Connect until storage is ready. It also requests an IDBFS writeback after verified pack/registry sync reaches world join, plus on Disconnect. Only settings, logs, and verified server pack cache data are in scope for V1. Browser local worlds and local multiplayer pack overrides are intentionally out of scope.

On a clean browser profile, the first successful join downloads the required `.apack` bundle into `/arvane_home/cache/server-packs/<bundle-hash>/` and writes the verified cache marker last. A page reload or reconnect creates a fresh session over the same browser-backed `/arvane_home`; if the cache marker and file hashes still verify, the client reports the bundle as `AlreadyCached` and skips `ServerboundPackRequest`. Missing, partial, or corrupted cache entries are not trusted and are handled by the normal pack-sync download/verification path.

## Local Verification

The verified V1 smoke flow is:

```sh
source /home/hippo/emsdk/emsdk_env.sh
emcmake cmake -S . -B build-web -DARVANE_BUILD_WEB=ON
cmake --build build-web --target arv_web
cmake --build build --target arv_dedicated
python3 -m http.server 8080 --directory build-web/apps/arv_web/dist
```

Start `arv_dedicated` with `network.web.enabled: true`, open `http://localhost:8080/`, and connect to `ws://localhost:42070/arvane` or the configured listener URL. A successful run reaches `Ready (storage mounted)`, `protocol handshake`, `registry sync`, `in game`, `remote chunk mesh uploaded`, and `remote entity markers uploaded`. Reloading the page and reconnecting should report a verified cached pack root and skip redownloading clean cache entries.

## Debugging

- Browser status messages appear in the page panel and console.
- `_arv_web_crypto_self_test` is exported for browser-console diagnostics; it should return `0`.
- The Connect button creates a browser `WebSocketClientTransport`, wraps it in `arv::client::ClientSession`, and pumps that session from the frame loop.
- The thin session sends `ServerboundHello`, signs `ClientboundAuthChallenge`, handles server hello/auth results, plans pack sync, downloads required APACK bundles, installs them into `/arvane_home/cache/server-packs`, verifies registry snapshots, sends `ServerboundPackReady`, records `ClientboundWorldJoinResult`, sends initial chunk interest, and stores received chunk section payloads for the renderer.
- The thin session tracks basic replicated entity spawn/despawn/snapshot mirrors after world join. The fallback renderer draws those mirrors as simple upright markers; textured or animated entity models are still pending.
- Reconnect and Disconnect request an IDBFS writeback so verified cache metadata survives page reloads.
- The thin session rejects handled server packets that arrive in the wrong login/registry/world phase and ignores stale packets already queued after a close event.
- Click the canvas to request pointer lock.
- Pointer-locked mouse movement updates the outgoing view angles. WASD or arrow keys move, Space jumps, Shift sneaks, Ctrl sprints, F requests a fly toggle, and mouse buttons map to existing player action flags.
- The browser frame loop sends movement frames through `ClientSession::submit_player_move` at the normal client movement send rate after world join.
- `ClientboundPlayerMoveCorrection` packets are drained from `ClientSession`; the web shell snaps predicted position, velocity, and view angles back to the server-authoritative values.
- If the browser WebSocket backlog is full, latest-wins movement frames are coalesced so stale input is replaced rather than blocking the frame loop.
- Received server chunk sections are meshed by a minimal WebGL2 fallback renderer as opaque colored cubes, with at most one section uploaded per frame; replicated entity mirrors are drawn as simple upright markers.
- Resize the browser window to verify canvas and WebGL viewport resizing.
- Server logs print the WebSocket bind address and path when the listener is enabled.
- Text, unmasked, oversized, malformed, or wrong-path WebSocket clients are rejected before they can enter gameplay state.
- If the browser reports storage unavailable, reload the page or clear site data before testing pack-cache behavior.
- Use server logs for pack-sync, registry hash, and disconnect diagnostics.

## Limitations

- No local worlds in the browser.
- No browser-hosted server.
- No WebGPU, WebTransport, WebRTC, or pthread-required WASM build.
- No browser GameNetworkingSockets.
- The current target is a verified WebGL2 fallback renderer for authoritative chunk cubes and replicated entity markers. Textured pack-backed rendering, entity animation/model rendering, and remounting downloaded packs into render resources remain follow-up milestones.
