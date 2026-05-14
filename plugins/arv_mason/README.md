# Mason

Mason is a first-party external Arvane plugin for world editing and blueprint authoring. It lives under `plugins/arv_mason` and links only against the standalone Plugin SDK target, `Arvane::PluginSDK`.

Pack ID: `arv_mason`
Namespace: `arv_mason`
Version: `0.1.0`

## Commands

Root command:

```text
/mason help
/mason status
```

Native command definitions are registered in code. Short aliases live in
`pack/data/commands.yaml`.

Selection:

```text
/mason select a
/mason select b
/mason select clear
/mason select info
/mselect <a|b|clear|info>
```

The current SDK command context does not expose a targeted block yet, so V1 uses the player's current block position for selection points.

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
/mason blueprint save <name>
/mason blueprint load <name>
/mason blueprint paste <name>
/mason blueprint list
/mason blueprint info <name>
/mbp <save|load|paste|list|info>
```

Blueprint names are limited to letters, numbers, `_`, `-`, and `.`. Path traversal, absolute paths, separators, and drive prefixes are rejected.

## Permissions

Mason registers these custom permission nodes:

```text
mason.command.use
mason.command.select
mason.command.set
mason.command.replace
mason.command.copy
mason.command.paste
mason.command.undo
mason.command.redo
mason.command.blueprint.save
mason.command.blueprint.load
mason.command.blueprint.paste
mason.command.blueprint.list
mason.command.blueprint.info
mason.limit.bypass
mason.region.bypass
```

Mutating commands also check the core `can_modify_world` permission through the Plugin SDK PermissionsAPI. Mason does not use game mode checks for authority.

## Config

Mason loads `arv_mason:config/main` through ConfigAPI. Missing or invalid config falls back to safe defaults and Mason attempts to save the default document.

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

## Blueprint Format

Mason authors Arvane Blueprint documents with the `.abp` extension and `format: arvane:blueprint`. V1 uses a palette plus RLE block encoding. Blocks are ordered as:

```text
index = x + size.x * (z + size.z * y)
```

The persistent storage API currently stores blueprint payloads in world-scoped plugin data keys while preserving the `.abp` document text. A later file-storage SDK can expose direct world-local `.abp` files without changing the codec.

## Safety Notes

Large operations enforce hard limits unless `mason.limit.bypass` is allowed. Operations above the confirmation threshold require `/mason confirm`. Large mutation plans are applied in scheduler-backed sync batches when they exceed `max_operation_blocks_per_tick`.
