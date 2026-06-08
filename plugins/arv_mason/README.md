# Mason

Mason is a first-party external Arvane plugin for world editing. It lives under
`plugins/arv_mason` and intentionally depends only on the Arvane Plugin SDK.
Player selections and blueprints are first-class engine/game systems exposed by
`arv_base` commands.

Pack ID: `arv_mason`
Namespace: `arv_mason`
Version: `0.1.0`

## Install

Build and install Mason into the development runtime home with:

```text
cmake --build build --target arv_mason_install_pack
```

The target copies `plugins/arv_mason/pack/` to
`arvane_home/packs/arv_mason/` and installs the current platform native library
under the pack's `native/<platform>/` directory. Enable the pack with:

```yaml
enabled:
  - "arvane_base"
  - "arv_mason"
```

Native loading is controlled separately by runtime policy:

```yaml
native_plugins:
  enabled: true
  allowed_packs:
    - "arv_mason"
```

For the split config layout, keep that policy in
`arvane_home/config/arvane.yaml` so both desktop and dedicated profiles see the
same local trust decision.

## Commands

Root command:

```text
/mason help
/mason status
/mason marker
/mmarker
/mm
```

Native command definitions are registered in code. Short aliases live in
`pack/data/commands.yaml`.

Selection:

```text
/select <a|b|clear|info>
/sel <a|b|clear|info>
```

The selection command is provided by `arv_base`, and Mason reads the current
selection through the Plugin SDK.

`/mason marker` adds a wood sword marker item to your inventory. Marker items
are identified by `arv_mason.marker = true` in stack data. Left click a block
with the marker to set point A; right click a block to set point B.

Editing:

```text
/mason set <block_state>
/mason replace <from> <to>
/mset <block_state>
/mreplace <from> <to>
```

Clipboard and history:

```text
/mason copy
/mason paste [--air|--no-air]
/mason undo
/mason redo
/mcopy
/mpaste [--air|--no-air]
/mundo
/mredo
```

Blueprints:

```text
/blueprint save <name>
/blueprint load <name>
/blueprint paste <name>
/blueprint list
/blueprint info <name>
/bp <save|load|paste|list|info>
```

Blueprint commands and `.abp` files are provided by `arv_base` and
`arv_blueprint`, not by Mason.

## Permissions

Mason registers these custom permission nodes:

```text
mason.command.use
mason.command.marker
mason.command.set
mason.command.replace
mason.command.copy
mason.command.paste
mason.command.undo
mason.command.redo
mason.limit.bypass
mason.region.bypass
```

Selection and blueprint permissions are owned by the base game commands.

Mutating commands also check the core `can_modify_world` permission through the Plugin SDK PermissionsAPI. Mason does not use game mode checks for authority.

## Config

Mason loads `config.yaml` and `messages.yaml` through the Plugin SDK ConfigAPI
file helpers. They resolve to:

```text
arvane_home/config/plugins/arv_mason/config.yaml
arvane_home/config/plugins/arv_mason/messages.yaml
```

Missing or invalid config falls back to safe defaults, and Mason attempts to
save the default document back to the same plugin config directory.

Default limits:

```yaml
limits:
  max_selection_blocks: 250000
  max_clipboard_blocks: 250000
  max_blueprint_blocks: 1000000
  max_history_entries: 15
  max_operation_blocks_per_tick: 8192
behavior:
  default_paste_air: false
  require_confirmation_above_blocks: 100000
  save_selection_per_player: true
  save_clipboard_per_player: false
  show_progress_messages: true
blueprints:
  default_scope: world
```

The blueprint and selection-persistence fields are retained for config format
compatibility. Mason no longer uses them; `arv_base` owns those systems now.

Default messages start with:

```yaml
prefix: "<red>Mason<gray>></>"
example-message: "{arv_mason_prefix} My message"
```

The prefix is registered as the resolved global variable
`arv_mason_prefix` when the server runtime starts. Because color tags replace
the active color, the single `</>` in the default prefix closes gray and resets
the following message text.

## Blueprint Format

The reusable Arvane Blueprint data model and `.abp` codec live in the engine
module `arv_blueprint`. V1 uses a palette plus RLE block encoding. Blocks are
ordered as:

```text
index = x + size.x * (z + size.z * y)
```

Base-game blueprint commands save world-local `.abp` files under the current
world's blueprint directory.

## Safety Notes

Large operations enforce hard limits unless `mason.limit.bypass` is allowed. Operations above the confirmation threshold require `/mason confirm`. Large mutation plans are applied in scheduler-backed sync batches when they exceed `max_operation_blocks_per_tick`.
