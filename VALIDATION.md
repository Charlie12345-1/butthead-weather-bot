# Butthead commissioning — 28 September 2026

## Hardware and recovery

- USB COM3, VID:PID 303A:1001; ESP32-S3 revision 0.2, 16 MiB flash, 2 MiB PSRAM. Device identifiers are omitted from this public report.
- Original firmware identified itself as `heltec-v4`, `2.7.23.b246bcd`. The custom environment inherits that exact board environment, including V4.3 KCT8103L automatic detection. Boot reported front-end LNA control `1`, as expected for this revision.
- Source baseline: official tag `v2.7.23.b246bcd`, commit `b246bcd72ec6989b803da0d83d217b045303d3c6`; branch `butthead-weather-bot`. No newer stock firmware was installed.
- Original configuration and complete 16 MiB flash were backed up privately. Official recovery archive integrity was checked against its published GitHub asset digest. The private backup is retained locally and is not part of this repository.
- Only the verified active app0 at `0x10000` was written. No erase, factory image, partition replacement, or filesystem replacement was performed. Esptool verified the written application hash.

## Build and isolated tests

- `heltec-v4-weather` build succeeded using the 2.7.23 toolchain and pinned dependencies, adding ArduinoJson 6.21.5.
- Application size: **2,157,136 bytes**, within the 6,553,600-byte app partition.
- Application SHA-256: `2c105802f092e7499b582651dd07d29f33d13fc70206495261ea686d8df4d164`.
- Production C++ parser/scheduler tests passed on the computer, using synthetic edge cases and captured public Open-Meteo/ECCC fixtures. No radio code is linked into that executable.
- Cases include new/unchanged/updated alerts, identifier churn, severity change, ended/expired alerts, malformed/stale/incomplete data, wrong geography, morning/evening output, overnight low, tomorrow preview, payload bounds, both 2026 DST transitions, and already-completed schedule slots.
- The same internal self-tests passed on the ESP32-S3 at startup, without RF output.

## Live device observations

| Check | Observed result |
|---|---|
| Meshtastic and LoRa startup | Firmware identifies as 2.7.23.b246bcd / heltec-v4-weather; SX1262 initialization succeeds. |
| Radio configuration | US region, LongFast, original power/hop settings retained. All channel records and the identity private key match the private pre-change snapshot. |
| Name | Butthead / BTHD. |
| Bluetooth with Wi-Fi | Windows discovered the tested node, connected, and enumerated the Meshtastic GATT service, both before and after live weather downloads. |
| Wi-Fi | Connected using Meshtastic's existing network settings; local TCP API also returned node identity/configuration. |
| Clock | Initial NTP failure remained quiet on RF; the normal five-minute retry acquired NTP time. |
| Schedule | Device logged `2026-09-28 19:00 EDT` as its next report, with RTC quality NTP. Saved timezone is `EST5EDT,M3.2.0/2,M11.1.0/2`. |
| Official alerts | Both London/Eastern Middlesex and Western Middlesex returned HTTP 200 and valid complete snapshots, with zero active alerts at the time checked. |
| Forecast | Open-Meteo returned HTTP 200; the board parsed current conditions, today's forecast and tomorrow's preview. |
| Receive/repeat | Received neighboring-node packets, repeated multiple packets, and heard its own node-information packet rebroadcast by a neighbor. |
| Worker resources | Minimum observed worker stack headroom during commissioning: 6,516 bytes; free heap after the test request: 86,168 bytes. These are observations, not a long-term soak guarantee. |

## One actual test broadcast

Exactly one weather test was handed to the normal Meshtastic `MeshService`, on primary channel index 0, as `TEXT_MESSAGE_APP`, without requesting acknowledgements. Payload: 105 bytes; hop limit: 3. Packet identifiers are omitted from this public report.

> TEST: London 7AM: 20C feels 18, clear. Today 22/8C overcast, precip 0%; N 13km/h. Tomorrow 24/10C fog 0%.

This commissioning message exercised the morning formatter outside its scheduled time. The radio logged `Completed sending` for that ID, then received the same packet rebroadcast by a neighboring node (hop limit 2, RSSI -27 dBm, SNR 5.75 dB). This verifies RF reception by a peer, without claiming that a person read it.

No fabricated alert was transmitted. Routine node information and routing traffic continued normally and are separate from the one weather test.

## Final persistence check

- A repeated local test request before reboot logged `Weather one-time test already completed; duplicate suppressed` and sent no weather packet.
- One deliberate software reboot increased the boot counter by exactly one. LoRa, Bluetooth, Wi-Fi and NTP initialized again. Both ECCC districts and Open-Meteo returned HTTP 200; the next report remained `2026-09-28 19:00 EDT`.
- After reboot, status reported `enabled=1 wifi=1 test_done=1`. Another test request was suppressed. No second `Weather TX` occurred.
- A log-only preview encountered a transient HTTP connection error (`-1`). The bot logged a quiet 300-second retry, kept routing neighboring traffic, and did not transmit an error. Further manual previews returned HTTP 200 and parsed correctly. Free heap returned to 86,220 bytes; the recorded low-water mark was 24,348 bytes during network work.
- Native USB control with reset signals inactive preserved the boot count between diagnostic connections. No uncommanded restart or crash loop was observed during commissioning.

## Practical limits

- The actual 07:00 and 19:00 wall-clock broadcasts were not awaited. Their production scheduler/formatting logic was tested, the next on-device deadline was checked, and the real send path was exercised once.
- There was no active London/Middlesex warning during commissioning. Real empty district responses were checked live; active-alert parsing/change detection was tested internally using fixtures without RF output.
- Bluetooth discovery, connection and the Meshtastic service were checked from Windows. A phone-app interaction was not performed.
- Short commissioning checks cannot prove indefinite uptime. Network or TLS failures remain silent and retry later; missing a report is preferable to sending stale data or disrupting Meshtastic.
- Windows' default serial-control signals can reset this board. The included local control helper holds those signals inactive. Only one API client should be used at a time during diagnostics.

## Publication checks

The public copy changes documentation and requires an explicit node ID in the local control helper. The firmware source remains identical to the source used for the validated binary. Device identifiers, private backups and raw radio logs are excluded.
