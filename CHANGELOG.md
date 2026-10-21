# Changelog

## 2026-10-21
- Licensed under EPL-2.0.

## 2026-10-20
- Gateway-side pairing: discovery scan, address allocation from the declared addresses, discovery response,
  confirmation by heartbeat or status reply, `on_device_paired` trigger. Fixed the discovery scan body (default-key
  XOR, as the native library does).
- `tools/brmesh_backup.py`: decode the app's backup into mesh key, devices, rooms and YAML.
- Public README, walkthrough and protocol reference.

## 2026-09-16
- Bulb status replies decoded (layout confirmed on 17 bulbs); grouped entities aggregate fresh member reports only.
- Relayed frames recognised instead of rejected.
- Bulb-side animations exposed as Home Assistant effects (21 presets + `brmesh_mode`).
- Fixed Home Assistant "unsupported color mode brightness" by advertising RGB + color temperature only.

## 2026-09-14
- Transmit engine rewrite: continuous airtime per packet (`command_airtime`), per-entity preemption, retry of
  dropped writes, automatic BLE stack restart, scanning only when needed, max TX power, random sequence seeds.
