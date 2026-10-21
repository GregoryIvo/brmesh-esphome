# Walkthrough: BRMesh lights in Home Assistant, from zero

This takes you from a box of BRMesh bulbs (or an existing BRMesh app install) to Home Assistant entities with
real state and effects. Budget an hour the first time. Read it once before starting; the only step that cannot be
redone later is choosing a mesh key for a brand-new mesh.

## 0. What you need

- An ESP32 board with Bluetooth LE. The ESP32-C3 dev boards (4 MB flash) are cheap and proven here. A board with
  an external antenna is a good idea for more than a dozen bulbs or outdoor installs.
- Home Assistant with the ESPHome add-on (or ESPHome CLI on a computer).
- Your BRMesh bulbs. Two situations:
  - **Already paired with the BRMesh app**: you need the app's mesh key and each bulb's short address. Section 2
    shows three ways to get them. You keep using the same key, and the app keeps working alongside.
  - **New, never paired**: you choose a mesh key yourself and let the gateway pair them (section 6). The app is
    never needed.

## 1. Install the component

1. In the ESPHome add-on, open the file editor (or use Samba / Studio Code Server) and create a folder
   `components/brmesh` next to your device YAML files, i.e. `/config/esphome/components/brmesh/`.
2. Copy these six files into it from this repository's `components/brmesh/`:
   `__init__.py`, `light.py`, `brmesh.h`, `brmesh.cpp`, `brmesh_protocol.h`, `brmesh_protocol.cpp`.
3. Whenever you update the component later, copy all six again and use **Clean Build Files** on the device
   before installing. The boot log prints `Component version:` so you can tell which build is running.

## 2. Get your mesh key and addresses

Skip to section 6 if your bulbs were never paired.

### Option A: app backup (no cables, recommended)

1. In the BRMesh app open the third tab (settings), choose the backup / export entry and share the file to
   yourself. It is named like `BRMESH_BACKUP20260914.json`.
2. On a computer with Python 3:

   ```bash
   pip install cryptography
   python3 tools/brmesh_backup.py BRMESH_BACKUP20260914.json
   ```

3. The output shows the mesh key as `brmesh_mesh_key: "xxxxxxxx"`, a table of bulbs with their short addresses and
   rooms, and a YAML `light:` block with one entity for all bulbs and one per room. Keep the key private.

### Option B: Android logcat

With USB debugging enabled and `adb` installed, run `adb logcat | grep getPayloadWithInner` and change a light in
the app. Lines like `payload:220635000000000000000000, key: 41424344` give the key (`41424344`) and, for direct
commands beginning `22`, the bulb address (`06` = address 6).

### Option C: you already know it

Older guides had you read the key from a rooted phone's shared preferences (`Ctrlkey`). The app stores it as four
characters; the hex in YAML is their ASCII codes (`ABCD` becomes `41424344`). The decoder script handles this.

## 3. Write the YAML

Create a new ESPHome device (name it, say, `brmesh-controller`) and replace its content with
[`examples/brmesh-controller.yaml`](../examples/brmesh-controller.yaml), adjusting:

- `esp32:` board/variant for your hardware.
- `external_components:` path `components` (this is relative to the YAML file).
- `brmesh.key` from your secrets.
- The `light:` entities: paste the block from the backup decoder, or write `addresses: [1, 2, 3, ...]` by hand.
  Add a few spare addresses if you plan to pair more bulbs later (section 6).

Add to `secrets.yaml`:

```yaml
brmesh_mesh_key: "41424344"
```

A good starting `brmesh:` block for daily use:

```yaml
brmesh:
  id: brmesh_hub
  ble_id: brmesh_ble
  key: !secret brmesh_mesh_key
  command_airtime: 1500ms
  group_refresh_interval: 0s
  query_after_write: true
  query_redundancy: 2
  state_query_on_boot: true
  state_poll_interval: 5min
  recover_ble: true
  retry_dropped_writes: true
```

## 4. Install and verify

1. Install to the board (USB the first time, OTA afterwards).
2. Open the log. At boot you should see:

   ```
   [C][brmesh]: BRmesh Hub:
   [C][brmesh]:   Component version: 2026-09-16 continuous-airtime
   [C][brmesh]:   Command airtime: 1500 ms (continuous advertising per packet)
   [C][brmesh]:   Registered light count: 17
   ```

3. Toggle the light in Home Assistant. In the log, a group command looks like this:

   ```
   Queued BRmesh tx type=5 ... airtime=450ms ... payload=45fd01...   <- temporary group membership
   BRmesh group light write count=17 state=ON ... payload=932aa8fd...
   Queued BRmesh tx type=5 ... airtime=1500ms ...                   <- the control packet
   BRmesh device upload addr=8 subtype=0 data=33000000ff00          <- a bulb answering the status query
   ```

4. If bulbs do not react, see Troubleshooting in the README. The two usual causes are an old component copy
   (no `Component version` line) and distance (replies weaker than -95 dBm).

## 5. Make it feel right in Home Assistant

- Use the group entity for automations and dashboards. Per-bulb entities are optional; add them with
  `address: N` if you want individual control.
- The effect dropdown lists the bulbs' animations. "None" returns to normal.
- For voice assistants, expose the group light; brightness and color work as with any light.
- Do not call `light.turn_on` with two color descriptors at once (for example `color_temp` and `rgb_color`);
  Home Assistant rejects that for every light, not just this one.

## 6. Pair new bulbs without the app

1. Make sure your YAML already declares the address the new bulb will get, for example `addresses: [1, 2, 3, 4]`
   when you have three bulbs and are adding a fourth. Install that YAML first.
2. If this is a brand-new mesh, pick any mesh key now (`brmesh_mesh_key: "41424344"`, any 8 hex digits) and keep
   it; all bulbs you pair will be bound to it.
3. Press **BRMesh Start Pairing** (button from the example YAML). The log prints the next free address.
4. Power the bulb on. A factory-fresh bulb answers within a few seconds. A bulb that was paired to another
   key (the app's, or a previous gateway) must be factory reset first; most BRMesh bulbs reset after five quick
   power cycles and blink to confirm.
5. Log sequence for success:

   ```
   BRmesh new bulb did=a1b2c3d4e5f6 type=43564 rssi=-70 assigned address 4
   Sent BRmesh discovery response did=a1b2c3d4e5f6 addr=4
   BRmesh heartbeat addr=4 group=0
   BRmesh bulb did=a1b2c3d4e5f6 paired as address 4 (confirmed by heartbeat)
   ```

6. Press **Stop Pairing** or wait five minutes. The bulb now follows the group entity.

Moving an existing app-paired mesh entirely to the gateway: nothing to do. Keep the app's key, the bulbs do not
care who sends the commands. You can uninstall the app once the backup decoder has given you the addresses.

## 7. Day-two notes

- **Power cuts**: bulbs come back on at full brightness. With polling on, Home Assistant notices within a poll
  interval and your automations can react.
- **Multiple rooms**: one `brmesh` light per room with its own `addresses` list. Groups share temporary group
  253, which is why membership is refreshed before each command by default.
- **Two gateways**: fine, they just transmit; keep `state_poll_interval` on only one of them to avoid double
  traffic.
- **Keeping the component current**: copy the six files, clean build files, install, check `Component version`.
