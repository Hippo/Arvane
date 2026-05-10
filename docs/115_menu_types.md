# Menu Types

## Purpose

Arvane uses `arv_ui` for player-facing menus. These menus are part of the game UI layer and must not be implemented with ImGui.

ImGui is reserved for developer/debug tooling only.

## Menu Implementation Rules

All menus in this document must:

* be implemented using `arv_ui`
* support keyboard and mouse navigation
* respect configurable key binds where applicable
* avoid directly mutating world/server state from UI callbacks
* send menu actions through explicit commands/events
* keep rendering, layout, and menu logic separate from game simulation logic

Menu screens should be reusable and data-driven where practical, but they do not need to be fully mod/plugin-extensible in the first implementation.

## Main Menu

The main menu is shown when the game starts and whenever the user returns from a world/server session.

Buttons:

```text
Single Player
Multi Player
Settings
Quit
```

Behavior:

* `Single Player` opens the single player menu.
* `Multi Player` opens the multi player menu.
* `Settings` opens the settings menu.
* `Quit` exits the game.

## Single Player Menu

The single player menu shows locally available saves/worlds.

UI elements:

```text
World List
Create World
Delete World
Back
```

Behavior:

* The world list displays available singleplayer saves.
* Selecting a world enables actions for that world.
* Double-clicking or pressing Enter on a selected world may start that world.
* `Create World` opens the create world menu.
* `Delete World` deletes the selected world after confirmation.
* `Back` returns to the main menu.

Deletion must not happen immediately from the first button press. It should use a confirmation dialog or confirmation screen.

## Create World Menu

The create world menu creates a new singleplayer save.

UI elements:

```text
Name Text Box
Seed Text Box
Create
Cancel
```

Behavior:

* `Name` is the user-facing world name.
* `Seed` is optional.
* Empty seed means the engine generates a random seed.
* Non-empty seed is parsed into the world generation seed.
* `Create` validates the inputs, creates the save, and starts or returns to the single player menu depending on final UX choice.
* `Cancel` returns to the single player menu without creating anything.

Validation:

* World name must not be empty.
* World name must be sanitized before being used as a folder name.
* Duplicate save names must be rejected or resolved through an explicit overwrite/rename flow.
* Invalid seed text must show a user-facing error.

## Multi Player Menu

The multi player menu shows saved server entries.

UI elements:

```text
Server List
Add Server
Edit Server
Remove Server
Back
```

Behavior:

* The server list displays user-added multiplayer servers.
* Saved multiplayer servers are loaded from and written to `arvane_home/config/server_list.json`.
* Selecting a server enables edit/remove/connect actions.
* Double-clicking or pressing Enter on a selected server may connect to it.
* Starting a remote join opens the multiplayer connecting screen while the
  transport and protocol handshake complete.
* `Add Server` opens the add server screen.
* `Edit Server` opens the edit server screen for the selected server.
* `Remove Server` deletes the selected server entry after confirmation.
* `Back` returns to the main menu.

Removing a server entry only removes the local saved entry. It must not affect the remote server.

## Multiplayer Connecting Screen

The multiplayer connecting screen is shown after a remote connection attempt
starts and before the client joins the world.

UI elements:

```text
Connecting Status
Server Name/Address
Cancel
```

Behavior:

* The screen blocks gameplay input and prevents rendering the pending remote
  session as a world.
* The client continues ticking networking and connection timeout logic while
  this screen is open.
* `Cancel` stops the pending connection and returns to the multi player menu.
* If the server does not respond before the configured connection timeout, the
  client switches to the multiplayer disconnect screen.
* Once the client reaches the in-game state, the screen closes and normal world
  rendering begins.

## Multiplayer Disconnect Screen

The multiplayer disconnect screen is shown after a remote connection ends before
or during gameplay.

UI elements:

```text
Reason Text
Reconnect
Server List
```

Behavior:

* The reason text shows the first clear disconnect reason reported by the client.
* `Reconnect` starts a fresh connection attempt to the remembered server entry.
* `Server List` returns to the multi player menu.
* If no server metadata is available, `Reconnect` is disabled and `Server List`
  remains available.
* The screen is used for server kicks, shutdown kicks, duplicate profile
  rejection, offline/unreachable servers, and keep-alive timeouts.

## Add/Edit Server Screen

Adding and editing a server share the same screen layout.

UI elements:

```text
Server Name Text Box
Address Text Box
Save
Cancel
```

Behavior:

* In add mode, `Save` creates a new server entry.
* In edit mode, `Save` updates the selected server entry.
* `Cancel` returns to the multi player menu without saving changes.

Fields:

* `Server Name` is user-defined and only affects the local server list.
* `Address` is entered in `ip`, `host`, `ip:port`, or `host:port` format.
* If the address does not include a port, the client uses the default server port `42069`.
* Example: `localhost` connects to `localhost:42069`.

Validation:

* Server name must not be empty.
* Address must not be empty.
* If a port is included, it must be a valid TCP/UDP port from `1` through `65535`.
* Invalid address text must show a user-facing error.

## Settings Menu

The settings menu is a navigation hub for configurable game options.

UI elements:

```text
Render Settings
Key Binds
Other Settings
Back
```

Behavior:

* `Render Settings` opens the render settings screen.
* `Key Binds` opens the key bind settings screen.
* `Other Settings` opens a general settings screen.
* `Back` returns to the previous menu.

Settings screens should use the same `arv_ui` system as the rest of the player-facing menus.

## In-Game Pause Menu

The pause menu opens with `Escape` by default. The key must be configurable through the key bind system.

UI elements:

```text
Resume
Back to Main Menu
Settings
Quit
```

Behavior:

* Opening the pause menu captures UI focus.
* `Resume` closes the pause menu and returns to gameplay.
* `Back to Main Menu` leaves the current world/server session and returns to the main menu.
* `Settings` opens the settings menu.
* `Quit` exits the game.

Singleplayer behavior:

* Opening the pause menu pauses the integrated singleplayer server/world.
* Closing the pause menu resumes the world.
* Settings opened from the pause menu should preserve the paused state until gameplay resumes.

Multiplayer behavior:

* Opening the pause menu does not pause the remote server.
* The local client may pause local input/camera control while the menu is open.
* Network processing must continue while the pause menu is open.

## Navigation Stack

Menus should be managed through a UI navigation stack or equivalent screen manager.

Required behavior:

* Opening a submenu pushes or replaces the active screen according to the intended flow.
* `Back` returns to the previous screen.
* `Cancel` returns without applying changes.
* `Escape` should generally behave like `Back` when inside menus, except where text input focus requires special handling.
* The main menu is the root menu and cannot be backed out of.

## Persistence

The following data should be persisted under `arvane_home/config/`:

```text
server_list.json for saved multiplayer servers
settings
key binds
```

Singleplayer worlds are listed from the save directory under `arvane_home/saves/`.

## Agent Notes

When implementing this system:

* Do not use ImGui for these menus.
* Use ImGui only for debug/developer tools.
* Reuse existing `arv_ui` primitives where possible.
* Add missing `arv_ui` widgets only when needed.
* Route button actions through explicit menu commands/events.
* Keep save discovery, server list persistence, and settings persistence outside raw UI widget code.
* Update relevant documentation if implementation details differ from this document.
