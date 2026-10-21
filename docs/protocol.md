# BRMesh / BroadLink FastCon protocol reference

Everything below was recovered from the BRMesh Android app (`cn.com.broadlink.blelight` SDK, `BLEFastconHelper`)
and its native library `libbroadlink_ble.so`, then confirmed on real bulbs where noted. Items marked *assumed*
are implemented but not yet observed on hardware.

## Transport

- BLE legacy advertisements, non-connectable, manufacturer specific data with company id `0xFFF0`.
- The phone advertises at ~100 ms interval, TX power high, for **3000 ms per command**, and stops the previous
  advertisement when a new command is sent. Bulbs de-duplicate by sequence number, so longer airtime is safe.
- Bulbs relay frames they receive (forward flag), re-signing the RF CRC (see below).

Two encodings exist on air after the `F0 FF` company id:

| Shape | Meaning |
|---|---|
| `A5 5A` + 22 bytes | "new protocol": plain FastCon body (4-byte header + 18-byte payload) |
| 24 bytes starting `6D B6 43 68 93 1D` | "legacy": whitened RF frame containing a 16-byte body (4 + 12) |
| 16 bytes | legacy body in plain form (seen from some devices) |

The gateway sends legacy frames for 12-byte payloads and new-protocol frames for 18-byte payloads or addresses
above 256.

### Legacy RF framing and whitening

`get_rf_payload`: `71 0F 55` + reversed FastCon address `C3 C2 C1` (all bytes bit-reversed) + body + CRC16
(polynomial 0x1021 variant over the address and the bit-reversed body, final bit-reversed and inverted). The
39-byte buffer (15 zero bytes preamble + 24) is whitened with the BLE LFSR seeded with 0x25; bytes 15..39 go on
air. Whitening is an XOR keystream, so de-whitening is the same operation after a 15-byte zero preamble.

**Relays**: a bulb re-broadcasting a frame keeps the body intact but its CRC equals the original CRC XOR
`0x79EF`. The gateway accepts these and flags them `relayed`.

## FastCon frame

```
header[0] = forward<<7 | command_type<<4 | high_address_nibble
header[1] = sequence
header[2] = mesh_key[3] when the body is encrypted, 0xFF otherwise
header[3] = (header[0] + header[1] + header[2] + sum(plain body)) & 0xFF
header ^= 5E 36 7B C4                   (fixed "default key")
body   ^= key[i % 4]                    (mesh key; DEFAULT key 5E367BC4 when the caller passes no key)
```

Command types:

| type | direction | use |
|---|---|---|
| 0 | gateway -> bulbs | discovery scan (pairing); 12 zero bytes, no mesh key (so XORed with the default key) |
| 1 | bulb -> gateway | discovery answer; plain body `did[6] type[2 LE] key[4] ...`; checksum not verified by the SDK |
| 2 | gateway -> bulb | discovery response; body encrypted with the **bulb's** key from the type-1 frame |
| 4 | bulb -> gateway | upload (status); `payload[0]` = address low, `payload[2]` = `len<<4 | subtype`, data at `payload[3]` |
| 5 | gateway -> bulbs | control; also seen relayed, and as heartbeats from bulbs |

Sequence numbers: the app seeds `sSendSeq` from the clock; the gateway seeds from a random number at boot.

## Control payloads (type 5)

Direct, 6 bytes in a 12-byte body:

| payload | meaning |
|---|---|
| `22 addr level 00 00 00` | brightness 0..127, 0 = off |
| `72 addr level B R G` | RGB (note the order: blue, red, green) |
| `82 addr level 00 cold warm` | white |

Group wrapper (`package_broadcast_control`): `0x33 + (len<<4)`, type low, type high, group id, command.
`type` is 43050 (`2A A8`) for lights. One-byte command (`0x43 ...`): `0x00` off, `0x80` on at the remembered
level, `1..127` brightness. Six-byte command (`0x93 ...`): `[0x80|level, B, R, G, warm, cold]`.

Temporary group 253 (`package_batch_temp_group`): `45 FD base token_lo token_hi bitmap[13]`, one packet per
104-address window; the app sends each window 3 x 150 ms and then the control packet. Fixed groups and rooms
exist in the protocol too but are not needed for Home Assistant.

Status query (`package_query_dev_state`): `00 40 selector 00 base bitmap[13]` (18-byte body), selector 0x0F; the
app airs it 300 ms per window. Time sync (`89 00 yy mm dd wd hh mm ss`) is sent by the app on resume and is
not required.

### Modes / animations (`package_scene_control`)

`0x28 + (steps<<4), addr, jiffies, color[steps]` for one bulb; `0x2C + (steps<<4), group, jiffies, color[steps]`
for a group (temporary group 253 works). Up to 9 steps. Color byte bits: cold 16, warm 8, green 4, red 2,
blue 1. The first color byte also carries `0x80` for flash (jump) versus fade and `0x40` ("grayscale", always set
by the app). Speed: fade modes use `jiffies = slider` (1..100), flash modes `jiffies = 200 - 2*(slider-1)`.
The app's 21 presets are listed in `components/brmesh/light.py`.

## Upload payloads (type 4) — confirmed 2026-09-16

Subtype 0, six data bytes: `[level 0..127 (0 = off), blue, red, green, warm, cold]`. Example from address 8 at
level 51 with warm 0x61 / cold 0x9E:

```
rx type=4 high=0 seq=180 payload=08006033000000619e000000000000000000
device upload addr=8 subtype=0 data=33000000619e
```

Bulbs bump their own sequence per upload and reply 100..600 ms after a query; with 17 bulbs at -87..-101 dBm about
half answer each 300 ms query. Still to confirm: the RGB byte order in uploads, and whether `data[4]` is warm.

## Pairing (type 0 / 1 / 2) — implemented, *assumed* until reported

1. Gateway: discovery scan (type 0) every 3 s while pairing.
2. Bulb in pairing mode: discovery answer (type 1) with its 6-byte `did`, 2-byte device type, 4-byte device key.
3. Gateway: `package_disc_res` body `did[6] addr 01 meshkey[4]` (18-byte form adds the group id at offset 12 for
   addresses above 256, type 43756 or high nibble above 2), command type 2, encrypted with the bulb's key, 3 s on
   air, repeated every ~2 s while the bulb keeps announcing.
4. Bulb: joins and sends a heartbeat: type 5 frame with `payload[0] & 0x0F == 4`, `payload[1]` = address,
   `payload[2]` = group, version bytes after. The app marks the bulb provisioned on this heartbeat.

The app never un-pairs over the air; bulbs are factory reset by power cycling.

## Known device types

`43564` RGB+CW light (the author's bulbs), `43756` gateway AC, `44601` radar. Many others exist in the SDK
(`BLE_DEV_TYPE_*` in `BLEFastconHelper`).

## Tooling notes for future work

- Decompile with jadx; the SDK is small and readable. The native library is stripped but exports mangled C++
  names; disassemble it in Thumb mode (`llvm-objdump --triple=thumbv7-none-linux-androideabi`).
- `adb logcat | grep jyq_` shows every packet the app sends (`getPayloadWithInnerRetry ---> payload:`).
- The app backup is AES-128-CBC with key `brgdbrgdbrgdbrgd`, IV `0392039203920300`, Base64; see
  `tools/brmesh_backup.py`.
