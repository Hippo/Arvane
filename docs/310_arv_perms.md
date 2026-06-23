# arv_perms

`arv_perms` is the first-party server permission administration plugin. It
ships default permission YAML in its pack `config` folder, then saves mutable
server config and per-player assignments through the Plugin SDK storage APIs. It
feeds the existing Player Overrides permission layer; it does not replace the
engine resolver.

## Files

The pack provides first-run defaults:

```text
arvane_home/packs/arv_perms/
  config/
    config.yaml
    groups.yaml
```

Dedicated server installs use the same layout under
`arvane_server/packs/arv_perms/`. In dedicated mode, mutable plugin config and
per-player data are saved under that server pack directory so the dedicated
server only writes below its server root:

```text
arvane_server/packs/arv_perms/
  config/
    config.yaml
    groups.yaml
  data/
    players/
      <PlayerProfileID>/
        permissions.json
```

Integrated singleplayer saves mutable config and per-player data inside the
active server save:

```text
arvane_home/saves/<server-save-name>/packs/arv_perms/
  config/
    config.yaml
    groups.yaml
  data/
    players/
      <PlayerProfileID>/
        permissions.json
```

Desktop plugin initialization without an attached server may still use the
shared SDK config root:

```text
arvane_home/config/plugins/arv_perms/
  config/
    config.yaml
    groups.yaml
  data/
    players/
      <PlayerProfileID>/
        permissions.json
```

Missing writable config files are generated from the loaded defaults when the
plugin loads. Player files are created individually as players are seen or
edited.

## config.yaml

```yaml
schema_version: 1
data:
  default_groups:
    - player
  operator_group: admin
  auto_save: true
  save_interval_seconds: 60
  track_last_known_names: true
```

`default_groups` are assigned when a known online `PlayerProfileID` has no
record yet. Player records are always keyed by `PlayerProfileID`, not display
name.

## groups.yaml

```yaml
schema_version: 1
data:
  groups:
    - id: player
      priority: 0
      inherits: []
      core:
        - name: can_interact
          value: ALLOW
        - name: can_open_inventory
          value: ALLOW
      permissions:
        - node: arvane.chat.send
          value: ALLOW
        - node: arvane.command.help
          value: ALLOW
        - node: arvane.command.msg
          value: ALLOW
    - id: admin
      priority: 1000
      inherits:
        - player
      core: []
      permissions:
        - node: "*"
          value: ALLOW
```

Higher priority groups override lower priority groups. Equal-priority conflicts
prefer `DENY`. Wildcard matching is still performed by the engine resolver.

## Per-Player Data

```json
{
  "schema_version": 1,
  "data": {
    "player_id": "0x0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
    "last_known_name": "example",
    "groups": ["admin"],
    "core_overrides": [
      {"name": "can_take_damage", "value": "DENY"}
    ],
    "permission_overrides": [
      {"node": "arvane.command.shutdown", "value": "ALLOW"}
    ]
  }
}
```

## Commands

Root command:

```text
/perm
/perms
```

Common commands:

```text
/perm group create <group>
/perm group delete <group>
/perm group list
/perm group info <group>
/perm group <group> priority <value>
/perm group <group> inherit add <parent>
/perm group <group> inherit remove <parent>
/perm group <group> set <node> allow|deny|unset
/perm group <group> unset <node>
/perm group <group> core set <core_permission> allow|deny|unset
/perm user <PlayerProfileID> group add <group>
/perm user <PlayerProfileID> group remove <group>
/perm user <PlayerProfileID> group set <group>
/perm user <PlayerProfileID> check <node>
/perm user <PlayerProfileID> check-core <core_permission>
/perm default add <group>
/perm default remove <group>
/perm save
/perm reload
/perm validate
```

The server console may run all commands. Player callers are checked against the
`arvane.command.perms` command node first, then against the existing resolved
custom permission API using `arvane.permissions.*` nodes for specific
subcommands.

## Notes

V1 scans online players on a short sync scheduler tick because the current
plugin event payloads do not expose detailed player-join data. Reload is
all-or-nothing: invalid files leave the live store unchanged.
