# BRMesh State Frame Notes

## Frame layout (from libbroadlink_ble.so, `parse_ble_broadcast_new`)

All frames share the FastCon header (4 bytes XOR `5E 36 7B C4`):

| byte | meaning |
|---|---|
| 0 | bit 7 forward flag, bits 6..4 command type, bits 3..0 high address nibble |
| 1 | sequence |
| 2 | `key[3]` when the body is encrypted, `0xFF` otherwise |
| 3 | checksum: sum of header bytes 0..2 and the decrypted body, mod 256 |

Body bytes are XORed with the 4-byte mesh key (repeating). Command types seen in the native parser:

| type | direction | body |
|---|---|---|
| 1 | device -> phone | discovery response: `did[6] type[2] key[4] ...` |
| 4 | device -> phone | **upload**: `addr_lo, reserved, (len << 4) \| subtype, data[len]`; `len == 0` means frame length - 7 |
| 5 | phone -> device (and relays) | control / full frame; `body[0] & 0x0F`: 4 = heartbeat, 11 = scene |

For type 4 the SDK calls `OnPassThroughCallback(addr, [subtype, data...])`; subtype 0 is forwarded to
`OnDevStateCallback(addr, 0, data)` ("device state upload"). `addr = addr_lo | (high nibble << 8)`.

On air there are three shapes after the `F0 FF` manufacturer id:

- `A5 5A` + 22-byte body: new protocol, plain body.
- 16-byte body: legacy protocol, plain body.
- 24 bytes starting `6D B6 43 68 93 1D`: legacy RF frame as phones transmit it (whitened). De-whiten with the
  0x25 LFSR after a 15-byte zero preamble; bytes 21..37 are the body, 37..39 the RF CRC16.

## Confirmed light state upload (captured 2026-09-16, 17 bulbs)

Every bulb answers the status query with a type-4 frame, subtype 0, six data bytes:

| data byte | meaning | evidence |
|---|---|---|
| 0 | brightness level 0..127, `00` when off (no 0x80 bit) | command level 69 -> `45`, 51 -> `33`, 13 -> `0d`, off -> `00` |
| 1..3 | blue, red, green (command order; not yet exercised, bulbs were in white mode) | |
| 4 | warm | command `619e` echoed as `619e`; HA decodes back to 285 mireds |
| 5 | cold | `ff00` at 500 mireds echoed as `ff00` |

Raw example, address 8 at level 51, warm 0x61 / cold 0x9e:

```
rx type=4 high=0 seq=180 payload=08006033000000619e000000000000000000
device upload addr=8 subtype=0 data=33000000619e
```

Bulbs bump their own sequence number for every upload. Replies arrive within about 100-600 ms of the query; with
a 300 ms query and the ESP32 at -87..-101 dBm from the bulbs, roughly half of the 17 answered each time. Raise
`query_redundancy` or move the gateway closer for more complete polls.

## Mesh relays

Bulbs re-broadcast the gateway's own legacy frames (`f0ff 6db64368931d ...`) with the FastCon body intact and the
RF CRC XORed with the constant `0x79EF`. The decoder accepts these, flags them `relayed=YES`, and counts them in
the config dump. Seeing them is proof the mesh heard and is forwarding a command.

## Capture procedure

1. Flash a build with `logger` level `DEBUG` and, under `brmesh:`, `listen: true`, `query_after_write: true`,
   `state_query_on_boot: true`.
2. Boot the gateway near one known light.
3. Toggle that light from Home Assistant.
4. Collect every `BRmesh rx ...` and `BRmesh device upload ...` line for 30 seconds, plus what the bulb did.
5. Repeat with the phone app on the same light.

## Still to confirm

- RGB byte order in uploads (send a pure red at 100 % in daylight and compare `data[1..3]` with `00ff00`).
- Whether `data[4]`/`data[5]` really are warm/cold rather than cold/warm: at 500 mireds the gateway sends
  `ff00`; the bulbs should look warm/orange, not blue-white.
