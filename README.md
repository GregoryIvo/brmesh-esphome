# BRMesh ESPHome

Control BRMesh / BroadLink FastCon Bluetooth mesh lights from Home Assistant with a single ESP32, no cloud,
no phone app in the loop.

BRMesh bulbs (sold under many brand names, paired with the "BRMesh" Android/iOS app) do not use normal
Bluetooth connections. They listen for encrypted BLE *advertisements* and relay them to each other. This
ESPHome external component makes an ESP32 speak that protocol, so your lights become ordinary Home Assistant
`light` entities with on/off, brightness, RGB, color temperature, real state feedback from the bulbs, and the
bulbs' built-in animations as effects. It can also pair new bulbs by itself.

> **Status:** used daily on a 17-bulb outdoor install (ESP32-C3). Transmit, state reporting, effects and the
> wire protocol are verified against the decompiled app. Gateway-side pairing is implemented from the app's
> code but has not yet been exercised on hardware; please report results.

New here? Read the [step-by-step walkthrough](docs/walkthrough.md). Curious how it works? See the
[protocol reference](docs/protocol.md).

## Features

- One Home Assistant light per room, group, or bulb. Groups are controlled with one mesh command, not a fan-out.
- On/off, brightness, RGB, cold/warm white.
- **Real state**: bulbs answer status queries, so Home Assistant shows what the bulbs are doing, including a
  bulb that missed a command or came back on after a power cut.
- **Effects**: the bulbs' 21 built-in fade/flash animations plus your own color sequences, running on the bulbs.
- **Pairing without the app**: put a bulb in pairing mode, press a button, done.
- Reliability engineered from the app's behaviour: 1.5 s continuous airtime per command, newest command wins,
  automatic retry and BLE stack restart, maximum TX power.
- Backup decoder: turn the app's export into your mesh key, addresses, rooms and a ready-made YAML.

## Hardware

Any ESP32 with Bluetooth LE. Tested on ESP32-C3 (`esp-idf` framework). Place it within a few metres of at
least one bulb; the bulbs relay to the rest of the mesh. Boards with an external antenna help in large
installs.

## Quick Start

1. Install ESPHome (the Home Assistant add-on works fine).
2. Copy the `components/` folder next to your device YAML (so you have `components/brmesh/...`).
3. Get your mesh key and bulb addresses. Easiest: export a backup from the BRMesh app and run

   ```bash
   python3 tools/brmesh_backup.py BRMESH_BACKUP2026xxxx.json
   ```

   It prints the key, every bulb with its address and room, and a YAML block to paste. Other ways are in
   the [walkthrough](docs/walkthrough.md#2-get-your-mesh-key-and-addresses).
4. Start from [`examples/brmesh-controller.yaml`](examples/brmesh-controller.yaml) and
   [`secrets.example.yaml`](secrets.example.yaml). The essential part:

   ```yaml
   external_components:
     - source:
         type: local
         path: components

   esp32_ble:
     id: brmesh_ble

   brmesh:
     id: brmesh_hub
     ble_id: brmesh_ble
     key: !secret brmesh_mesh_key      # 8 hex chars, e.g. "41424344"
     command_airtime: 1500ms
     query_after_write: true
     query_redundancy: 2
     state_poll_interval: 5min

   light:
     - platform: brmesh
       brmesh_id: brmesh_hub
       name: BRMesh All Lights
       addresses: [1, 2, 3, 4, 5, 6]
   ```

5. Install to the ESP32. The boot log should show `BRmesh Hub:` with `Component version:` and your
   registered addresses. Toggle the light in Home Assistant.

## Configuration Reference

### `brmesh:` (the hub)

| Option | Default | Meaning |
|---|---|---|
| `key` | required | Mesh key, 4 bytes as 8 hex characters. |
| `ble_id` | auto | The `esp32_ble` component to use. |
| `command_airtime` | `1500ms` | How long each control packet is advertised. The app uses 3 s; raise this if lights miss commands. |
| `group_refresh_interval` | `0s` | Re-send temporary-group membership before every group command (`0s`) or reuse it for this long. |
| `query_after_write` | `false` | Ask the bulbs for their state after every command. |
| `query_redundancy` | `1` | Query airtime multiplier (300 ms each). `2` gets replies from more bulbs. |
| `state_query_on_boot` | `false` | Query all bulbs once at boot. |
| `state_poll_interval` | `0s` | Periodic query of all bulbs (`5min` recommended). |
| `state_timeout` | `10s` | How long a query waits for replies. |
| `listen` | `false` | Keep the BLE scanner on even without query features (for logging bulb frames). |
| `recover_ble` | `true` | Restart the BLE stack after two consecutive dropped bursts. |
| `retry_dropped_writes` | `true` | Re-send a light's last state once after a dropped burst or BLE restart. |
| `pairing_addresses` | declared addresses | Addresses handed to new bulbs while pairing. |
| `pairing_timeout` | `5min` | Pairing mode switches itself off after this. |
| `on_device_paired` | | Automation with `address` (int) and `did` (string) when a bulb joins. |
| `redundancy`, `command_interval` | `5`, `300ms` | Legacy: their product is the airtime when `command_airtime` is unset. |

### `light:` platform `brmesh`

| Option | Default | Meaning |
|---|---|---|
| `addresses` | | List of short addresses this entity controls as one group. |
| `address` | | A single bulb instead. Give one of `address` / `addresses`. |
| `supports_cold_warm` | `true` | Expose color temperature. |
| `builtin_effects` | `true` | Add the app's 21 animations as effects. |
| `effects` | | Standard ESPHome effects list; see [Effects](#effects). |
| `default_transition_length` | `0s` | Keep at `0s`; BLE cannot stream smooth transitions. |

### Actions

`brmesh.start_pairing`, `brmesh.stop_pairing`, `brmesh.clear_pairings`, `brmesh.enroll_device`,
`brmesh.send_raw`, `brmesh.group_on_off`, `brmesh.temp_group_on_off`, `brmesh.send_scene`. The example YAML wires
the pairing ones to buttons.

## State And Groups

With `query_after_write` and `state_poll_interval` on, the bulbs answer status queries and the entities follow
their replies. A grouped entity is ON if any member that reported since the last command is on, and shows the
level and color of the most recent such report. Reports older than the last command are ignored so a slow bulb
cannot overwrite a fresh value, and a bulb that ignored an OFF command shows up as the group staying ON. If the
state options are off, entities are optimistic (they show what was requested).

Polling keeps the BLE scanner running. On a shared-radio chip such as the ESP32-C3 that costs a little transmit
reliability; if commands start missing bulbs, lengthen `state_poll_interval` first.

## Effects

The bulbs have built-in animations (the app's "Mode" screen). One command starts them, then the bulb animates on
its own. Every `brmesh` light gets the app's presets by default: Full color fade, Romantic, Fresh, Cozy, Full
color flash, Three color flash, the two-color flashes, Red/Green/Blue/White flash, Christmas, Winter, Halloween,
Valentines day. Pick one in Home Assistant; pick "None" or change the color to return to normal control.
Changing brightness during an effect re-sends the mode so it keeps running.

Custom sequences:

```yaml
light:
  - platform: brmesh
    brmesh_id: brmesh_hub
    name: Porch
    addresses: [1, 2, 3]
    builtin_effects: false
    effects:
      - brmesh_mode:
          name: Party
          preset: three_color_flash
          speed: 60               # 1 (app default) .. 100
      - brmesh_mode:
          name: Ocean
          style: fade             # fade or flash
          colors: [blue, cyan, white, green]
          speed: 10
```

Colors: `off, red, green, blue, purple, cyan, yellow, white, warm_white, cold_white`, up to nine per sequence.
Preset keys are the preset names in snake case (`full_color_fade`, `red_blue_flash`, `valentines_day`, ...).

## Pairing New Bulbs Without The App

ESPHome cannot create entities at runtime, so declare the address a new bulb will get **before** pairing:

```yaml
light:
  - platform: brmesh
    brmesh_id: brmesh_hub
    name: BRMesh All Lights
    addresses: [1, 2, 3, 4, 5, 6, 7, 8]    # 7 and 8 are spares for future bulbs

brmesh:
  pairing_timeout: 5min
  on_device_paired:
    - logger.log:
        format: "Paired bulb %s as address %u"
        args: [did.c_str(), address]
```

Then: press **BRMesh Start Pairing**, power the new bulb on, watch for `assigned address N` and
`paired as address N` in the log, press **Stop Pairing**. Bulbs previously paired with the app need a factory
reset first (typically five quick power cycles; check the bulb's leaflet). Addresses already bound to a bulb are
never handed out again; **Clear Pairing Cache** forgets the bindings so addresses can be reused.

Only bulb firmware updates still need the app.

## Troubleshooting

Set `logger: level: DEBUG` while investigating.

| Symptom | Look for | Fix |
|---|---|---|
| Nothing happens | `Component version:` missing from the boot log | The add-on compiled an old copy; replace `components/brmesh` and clean build files. |
| Some bulbs miss commands | `Sending BRmesh tx ... airtime=` | Raise `command_airtime` to `2500ms`; move the ESP32 closer; lower `state_poll_interval`. |
| Lights stop until a reset | `BRmesh BLE failed three times`, `restarting the BLE stack` | Recovery is automatic now. Constant restarts mean WiFi/BLE coexistence trouble: relocate the board. |
| HA state wrong | `BRmesh device upload addr=...` lines | Make sure `query_after_write: true`; replies at -95 dBm and below are lossy, move the ESP32. |
| `unsupported color mode brightness` in HA log | | Fixed in this version; update the component. |
| Effect does nothing | `BRmesh group mode ...` | Bulb must support modes (RGB bulbs do); try a flash preset first. |
| Pairing finds nothing | `BRmesh discovery from did=` | Bulb is not in pairing mode: power cycle it (new) or factory reset it (previously paired). |
| Pairing finds a bulb but never confirms | `did not confirm address` | The response did not reach it; move closer and retry. |

## How It Works

The ESP32 broadcasts BLE advertisements with manufacturer id `0xFFF0`. Each carries a FastCon frame: a 4-byte
header (type, sequence, key id, checksum) XORed with a fixed key, and a 12 or 18-byte body XORed with the mesh
key. Group commands use the app's temporary group 253: a membership bitmap packet, then the control packet.
Bulbs answer status queries with upload frames, relay each other's packets, and run animations locally.
Everything was recovered from the decompiled BRMesh app and its native library; the details, including the
pairing handshake and the reply layout, are in [docs/protocol.md](docs/protocol.md).

## Development

```bash
bash tests/run_protocol_tests.sh     # pure packet encode/decode fixtures
bash tests/run_runtime_tests.sh      # hub/light behaviour against ESPHome stubs
bash tests/component_static_test.sh  # config defaults
esphome compile examples/brmesh-17-lights.yaml
```

Contributions welcome: captures of frames from bulb models other than RGB+CW lights, pairing reports, and
anything that proves or disproves an assumption listed in `docs/protocol.md`.

## License

Eclipse Public License 2.0 (`SPDX-License-Identifier: EPL-2.0`). See [LICENSE](LICENSE). In short: use it, modify
it, ship it in your own projects; changes to these files that you distribute must be shared under the EPL.

## Credits And Disclaimer

Based on the original BRMesh ESP32 sketch idea and on reverse engineering of the BRMesh Android app
(`cn.com.broadlink.blelight` SDK). Not affiliated with BroadLink or the BRMesh app authors. The mesh key
controls your lights; keep it out of public repositories and screenshots.
