#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: apps/arv_web/manual_verify.sh [--check] [--serve]

Builds the native dedicated server and Emscripten arv_web target, then prints
the manual browser verification flow for V1 multiplayer testing.

Options:
  --check   Only check required commands and paths.
  --serve   After a successful build, serve the generated dist directory.

Environment:
  ARV_NATIVE_BUILD_DIR  Native build directory. Default: ./build
  ARV_WEB_BUILD_DIR     Emscripten build directory. Default: ./build-web
  ARV_WEB_HTTP_PORT     Static file server port. Default: 8080
  ARV_WEB_SERVER_URL    Browser WebSocket URL. Default: ws://localhost:42070/arvane
USAGE
}

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
native_build="${ARV_NATIVE_BUILD_DIR:-${repo_root}/build}"
web_build="${ARV_WEB_BUILD_DIR:-${repo_root}/build-web}"
http_port="${ARV_WEB_HTTP_PORT:-8080}"
server_url="${ARV_WEB_SERVER_URL:-ws://localhost:42070/arvane}"
check_only=0
serve=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --check)
      check_only=1
      ;;
    --serve)
      serve=1
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
  shift
done

require_command() {
  local name="$1"
  if ! command -v "${name}" >/dev/null 2>&1; then
    echo "missing required command: ${name}" >&2
    return 1
  fi
}

require_command cmake
require_command emcmake
require_command python3

if [[ ! -f "${repo_root}/CMakeLists.txt" ]]; then
  echo "could not find repo root CMakeLists.txt at ${repo_root}" >&2
  exit 1
fi

if [[ "${check_only}" -eq 1 ]]; then
  echo "manual verification prerequisites are available"
  echo "native build: ${native_build}"
  echo "web build: ${web_build}"
  exit 0
fi

cmake --build "${native_build}" --target arv_dedicated
emcmake cmake -S "${repo_root}" -B "${web_build}" -DARVANE_BUILD_WEB=ON
cmake --build "${web_build}" --target arv_web

dist="${web_build}/apps/arv_web/dist"
for artifact in index.html loader.js styles.css arv_web.js arv_web.wasm; do
  if [[ ! -f "${dist}/${artifact}" ]]; then
    echo "missing generated web artifact: ${dist}/${artifact}" >&2
    exit 1
  fi
done

cat <<CHECKLIST

Manual browser verification

1. Enable the dedicated WebSocket listener in arvane_server/server.yaml:

   network:
     web:
       enabled: true
       bind_address: 0.0.0.0
       port: 42070
       path: /arvane

2. Start the dedicated server in another terminal:

   ${native_build}/apps/arv_dedicated/arv_dedicated

3. Serve the web client:

   python3 -m http.server ${http_port} --directory ${dist}

4. Open:

   http://localhost:${http_port}/

5. Connect to:

   ${server_url}

6. Verify:

   [ ] browser storage reports ready before Connect
   [ ] WebSocket connects and reaches protocol handshake
   [ ] pack manifest is received
   [ ] clean cache downloads APACK bundle into /arvane_home/cache/server-packs
   [ ] registry snapshot verifies before world join
   [ ] world join succeeds
   [ ] server chunk sections render on the canvas
   [ ] pointer lock, mouse look, and WASD/Space/Shift/Ctrl send movement
   [ ] Disconnect cleans up without crashing
   [ ] page reload and reconnect reuse verified cache without redownload
   [ ] corrupting or clearing browser cache forces a safe redownload/reject path

CHECKLIST

if [[ "${serve}" -eq 1 ]]; then
  exec python3 -m http.server "${http_port}" --directory "${dist}"
fi
