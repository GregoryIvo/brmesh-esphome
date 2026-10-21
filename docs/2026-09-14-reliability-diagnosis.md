# Why the gateway was flaky: app vs gateway comparison (2026-09-14)

Sources: `brmesh_jadx/sources/cn/com/broadlink/blelight/helper/BLEFastconHelper.java`, the app's `log.log`
(`adb logcat`, 2026-07-30), and `libbroadlink_ble.so` (armeabi-v7a, Thumb disassembly with llvm-objdump).

## 1. Airtime: 500 ms vs 3000 ms

`BLE_CMD_SEND_TIME = 3000` and `BLE_CMD_RETRY_CNT = 1`. `doSendCommand` starts one advertisement
(`AdvertiseSettings`: mode 2 = LOW_LATENCY, tx power 3 = HIGH, connectable, timeout 0) and posts message 0
(`stopSending`) 3000 ms later. The logcat confirms it: every `Advertise start succeeds` is followed by
`doStopSend().stopAdvertising` 2.9-3.0 s later.

The gateway advertised each command for 5 repetitions of 100 ms, restarting the advertiser between them. Total
airtime 500 ms, one sixth of the app, with 15 extra GAP round trips per command.

Bulbs de-duplicate by sequence number, so longer airtime has no downside except latency.

## 2. Preemption

`doSendCommand` calls `doStopSend()` first and `sHandler.removeMessages(0/2)`: the newest command always replaces
the one on air. The gateway queued everything and a light entity waited for the *entire* queue to drain before
dispatching, so a fast slider or a toggle-while-busy produced long backlogs and stale commands.

## 3. No recovery path

After three BLE failures the gateway dropped the whole queue, kept the optimistic Home Assistant state, and never
retried or reset anything. That is exactly the "lights say ON but nothing happened until I reset the controller or
pressed All On" symptom. ESPHome's BLE event queue is 88 entries; the gateway scanned at 60 % duty cycle with
duplicates enabled all the time, so in a busy 2.4 GHz environment scan reports can crowd out the advertising
completion callbacks, which then time out (2 s) and count as failures.

## 4. Things the gateway already got right

- FastCon header/body encoding, checksum, default key, whitening and RF CRC: byte-identical to the app
  (`send---> payload / calculatedPayload` in the log reproduces exactly; covered by a test now).
- Group wrapper `43`/`93 2a a8 <group> ...`, temp group 253, membership `45 fd ...` bitsets and 104-address windows.
- Forward flag: the app's `jyq_forward: flag: 0` is the *global* override; `getForwardFlag(z)` returns `z`, and every
  control path passes `z = true`, so forward = 1 like the gateway.

## 5. What the app does not do

The BRmesh app never calls `queryDeviceStatusWithAddrs` and never registers `OnDevStateCallback`. It has no idea
what state the bulbs are in either; its UI is optimistic too. Bulb state replies, if they exist for these bulbs,
have never been observed in any capture. The decoder now matches the native parser's framing so a reply would be
recognised, but the data layout for lights remains a guess.

## Changes made

See `docs/reliability-testing.md` for the table and the acceptance test.
