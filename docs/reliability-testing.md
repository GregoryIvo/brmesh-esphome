# Reliability patch (2026-09-14): install and test

## Install with your existing Home Assistant configuration

1. Replace the complete `components/brmesh/` directory beside your installed ESPHome YAML with this version.
   Copy the Python files as well as the C++ files: the schema gained new options.
2. Keep your existing ESP32-C3 board settings, API/OTA credentials, light name, and addresses `1..17`.
3. Under `brmesh:` replace `redundancy` / `command_interval` with `command_airtime: 1500ms`. Old values still
   work (their product becomes the airtime), but `5 x 100ms = 500 ms` is too short; the phone app uses 3 s.
4. Keep `query_after_write: false`, `state_query_on_boot: false`, `state_poll_interval: 0s`,
   `all_group_refresh_on_boot: false`, and `listen: false` for daily use. With all of those off the gateway does not
   scan at all, which keeps ESPHome's BLE event queue free for the advertising callbacks.
5. Set `logger: {level: DEBUG}` during testing, compile, then install with your usual ESPHome workflow.
   Clean build files first so ESPHome does not reuse the old component sources.

The complete example is `examples/brmesh-17-lights.yaml`.

## What changed and why

Comparison against the decompiled app (`BLEFastconHelper.java`) and its `adb logcat` output:

| Behaviour | BRmesh app | Old gateway | New gateway |
|---|---|---|---|
| Airtime per control packet | 3000 ms continuous | 5 x 100 ms with stop/start between | `command_airtime` (1500 ms) continuous |
| Newer command while one is on air | stops the old one immediately | queued behind it, light waits for an empty queue | cancels the old one, dispatches at once |
| TX power | `ADVERTISE_TX_POWER_HIGH` | ESP-IDF default | `ESP_PWR_LVL_P9` |
| Sequence numbers after restart | seeded from clock | restart at 1 | seeded from random |
| Scanning | only on screens that need it | always, 60 % duty cycle | only while pairing / state features / `listen` |
| After repeated BLE failures | n/a (Android stack) | drop burst, HA keeps optimistic state, wait for manual reset | re-send last state once, then restart the BLE stack, then re-send once more |
| Reply framing | native `parse_ble_broadcast_new` | guessed | matches the native parser |

## Physical acceptance test

Allow about two seconds per group change with the default airtime (0.45 s membership + 1.5 s control).

1. From the normal **BRMesh All Lights** entity, perform 20 on/off cycles, pausing 3 seconds between commands.
   Record how many of the 17 lights respond on each cycle and any `BRmesh BLE` warnings.
2. Check white and RGB at 10%, 50%, and 100% brightness. Move the brightness slider quickly and confirm the final
   setting wins; the log should show `Superseded N queued packet(s)`.
3. Press **Group All Off**, then immediately turn on the normal light entity. Confirm the final command wins.
4. Change the lights using the phone app, then issue a normal Home Assistant light command. Membership refresh
   should let control resume without any button or reset.
5. Power-cycle the bulbs, allow them to boot, and repeat a normal Home Assistant command without resetting the ESP32.
6. Leave the system idle overnight and repeat normal control the next day.
7. Deliberately stress it: toggle the light ten times in quick succession. Every toggle must end with all bulbs in
   the final state. If you see `restarting the BLE stack`, note how often; occasional is fine, constant is not.

If some lamps still miss commands, raise `command_airtime` to `2500ms` or `3000ms` and repeat step 1. Record which
lamps, whether their position matters, and whether the phone app reaches those same lamps from the ESP32's location.

## State reporting (confirmed 2026-09-16)

Your bulbs answer the status query. Recommended daily settings once transmit reliability is confirmed:

```yaml
brmesh:
  command_airtime: 1500ms
  query_after_write: true
  query_redundancy: 2
  state_poll_interval: 5min
```

Polling keeps the BLE scanner on. If commands start missing lights with it enabled, that is the scanner competing
with the transmitter; lengthen `state_poll_interval` first.

## Original note before the capture

The group entity is optimistic unless bulbs answer. The frame layout for replies is now the same as the app's
native library, so if the bulbs ever transmit a state upload it will show up as `BRmesh device upload addr=...` in
the log and update Home Assistant. To look for replies, temporarily enable `listen: true`,
`query_after_write: true` and `state_query_on_boot: true` with DEBUG logging, toggle one light, and collect 30
seconds of `BRmesh rx` lines. The BRmesh app never queries bulb state itself, so it is possible your bulbs simply
never answer; that is not a gateway fault.
