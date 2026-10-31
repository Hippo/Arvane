# Arvane

Arvane is a voxel sandbox you can play on your own or with friends. Start a world from a seed, explore the terrain, and build with blocks and flowing water. Creative mode gives you flight and a block inventory to build from.

You can save structures as blueprints to place again later. For larger builds, the optional [Mason plugin](plugins/arv_mason/README.md) adds tools to fill areas, replace blocks, copy and paste, and undo edits.

Player profiles support custom PNG skins. Content packs can change textures and models, add gameplay definitions, or change how worlds generate. Servers send players the required client content when they join.

Arvane is still in development. Start with the desktop client; the browser client is experimental and currently uses simplified rendering.

## Getting started

Build Arvane from this checkout using the steps below. Keep the included `arvane_home/` directory alongside the source: it contains the base game content.

### Build the desktop client

You will need:

- A C/C++ build toolchain with C++20 support.
- CMake 3.19 or newer and Git.
- A graphics driver that supports OpenGL 3.3 Core, plus your platform's graphics and window-system development libraries.
- Internet access for CMake to download dependencies. You may also need platform development packages; CMake will report missing dependencies during configuration.

From the repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DARVANE_BUILD_TESTS=OFF \
  -DARVANE_BUILD_BENCHMARKS=OFF \
  -DARVANE_BUILD_SDK_EXAMPLES=OFF
cmake --build build --config Release --target arv_desktop --parallel
```

Launch the client from the repository root:

```sh
./build/apps/arv_desktop/arv_desktop
```

If you're using Visual Studio, look for `build/apps/arv_desktop/Release/arv_desktop.exe`.

See [Building and Tooling](docs/50_building_and_tooling.md) for dependency options.

### Your first world

1. On first launch, create a profile and choose a display name. You can import a skin here too.
2. Choose **Single Player**, then **Create World**.
3. Name the world and enter a numeric seed, or leave the seed blank to generate one.
4. Once the world loads, press E to open your inventory. Escape opens the menu.

To switch to Creative, press T and enter `/gamemode creative` in chat. Other built-in modes are Survival, Spectator, Lobby, and NoClip. On servers, your permissions determine which modes you can use. Type `/help` to see available commands.

## Desktop controls

You can change these bindings in **Settings → Controls**.

| Action | Control |
| --- | --- |
| Move | W / A / S / D |
| Look around | Mouse |
| Jump / fly upward | Space |
| Sprint | Left Shift |
| Sneak / fly downward | Left Ctrl |
| Toggle flight, when permitted | Double-tap Space |
| Break a block / primary action | Left mouse button |
| Place a block / use an item | Right mouse button |
| Select a hotbar slot | 1–9 or mouse wheel |
| Open inventory | E |
| Drop an item | Q |
| Open chat and enter commands | T |
| Show player list | Tab |
| Mount / dismount | F |
| Open menu / close a screen | Escape |

## Multiplayer

Choose **Multi Player** from the main menu and add the server's IPv4 address and port. If you're running the server on the same computer, use `127.0.0.1:42069`.

The client downloads and verifies the server's required content, then caches it for the next time you connect.

### Host a server

Build and start the server from the repository root:

```sh
cmake --build build --config Release --target arv_dedicated --parallel
./arvane_server/arv_dedicated
```

On Windows, run `arvane_server/arv_dedicated.exe`.

The build copies the executable into `arvane_server/`. On first startup, the server creates its data directories. If it has no packs installed, it copies the base pack from `arvane_home/`.

Server settings live in `arvane_server/server.yaml`. For example:

```yaml
world: default
network:
  bind_address: 0.0.0.0
  port: 42069
```

Allow incoming UDP traffic on port 42069 through your firewall. To let players connect from outside your network, forward that port to the server. Stop the server with Ctrl+C to save the world before it exits.

## World editing

The base game's `/select` and `/blueprint` commands let you select an area and save it as a reusable structure. Mason adds bulk editing and a clipboard, with undo and redo.

To install Mason into `arvane_home/packs/`:

```sh
cmake --build build --config Release --target arv_mason_install_pack
```

Follow the [Mason setup guide](plugins/arv_mason/README.md) to enable the pack and allow its native plugin to load. In game, use `/mason help` for commands or `/mason marker` to get a selection tool.

The guide also has [selection and blueprint examples](plugins/arv_mason/README.md#commands).

## Browser client

The experimental browser client needs a WebGL2-capable browser and a dedicated server with its WebSocket listener enabled.

It draws colored blocks and simple entity markers. It doesn't yet support full textured rendering, local worlds, or hosting a server in the browser. See the [browser client guide](apps/arv_web/README.md) to build it and connect.

## Your files

Local files are stored in `arvane_home/`. The dedicated server uses `arvane_server/`.

| Location | Contents |
| --- | --- |
| `arvane_home/packs/` | Installed local content packs, including the base game |
| `arvane_home/config/` | Local settings |
| `arvane_home/profiles.json` | Player profiles and skin preferences |
| `arvane_home/saves/` | Local saved games |
| `arvane_home/skins/` | Imported player skins |
| `arvane_home/keys/` | Private keys for your player identities |
| `arvane_home/cache/` | Cached server content and skins |
| `arvane_server/` | Dedicated server settings, packs, worlds, cache, and logs |

When backing up your worlds, include `profiles.json`, `skins/`, and `keys/` too. Your keys prove ownership of your profiles, so don't share them.

If Arvane can't find the base content, launch it from the repository root or pass `--arvane-home /path/to/arvane_home`.

## Documentation

- [Content packs](docs/270_pack_system.md)
- [Player profiles and skins](docs/290_player_profile.md)
- [Mason setup and commands](plugins/arv_mason/README.md)
- [Plugin API](docs/260_plugin_api.md) and [example plugins](examples/plugins/)
- [Building and Tooling](docs/50_building_and_tooling.md)
