# Butthead London weather bot

This extends real Meshtastic **2.7.23.b246bcd**, the firmware already proven on this particular Heltec V4.3. Branch: `butthead-weather-bot`. Build environment: `heltec-v4-weather`, inheriting the stock `heltec-v4` environment. The stock board definition, pins, partition layout, SX1262 driver and V4.3 KCT8103L front-end detection are unchanged. No newer stock firmware is needed or used.

## Behavior

- One short primary-channel report at **07:00** and **19:00 America/Toronto**, including daylight saving. Morning includes current/modelled conditions, feels-like, today's high/low, precipitation chance/amount, wind, and tomorrow. Evening includes the overnight hourly low/precipitation/wind and tomorrow's forecast.
- Forecast/current data: [Open-Meteo](https://open-meteo.com/en/docs), latitude 42.9849, longitude -81.2453. Current conditions are model-based. No API key. Units are Celsius, km/h, mm rain and cm snow. Data attribution: Open-Meteo, CC BY 4.0.
- Official alerts: [ECCC MSC GeoMet Weather Alerts](https://api.weather.gc.ca/collections/weather-alerts?f=html), filtered separately to the official **London - Parkhill - Eastern Middlesex County** and **Strathroy - Komoka - Western Middlesex County** forecast areas. These names are listed in [Canada's official area directory](https://www.canada.ca/en/environment-climate-change/services/weather-general-tools-resources/weatheroffice-online-services/atom-feeds.html). Each is checked every 15 minutes. GeoMet supplies official alert names, full English bulletin text, colour/severity, identifiers and expiration times. The bot sends a bounded excerpt, never an inferred warning. RSS was investigated but not used because its active-alert summaries can contain only issue timestamps.
- Successful complete alert snapshots update persistent state in ESP32 NVS. Semantic content fingerprints suppress unchanged entries, even if their issue identifier or publication/expiration time changes; changed official text or severity can announce again. Cleared, expired and ended alerts disappear quietly. Stale responses, incomplete/paginated snapshots and more than eight simultaneous entries per district are rejected without changing the last good state. Failed downloads never clear existing state. An alert covering both forecast districts may produce a separate district-specific message for each.
- Messages are at most 220 UTF-8 bytes. Bot transmissions are spaced by at least 60 seconds, honor the router's queue/channel utilization checks, request no acknowledgement, and use at most three hops. Existing user radio settings are preserved. No hourly weather messages or mesh diagnostics.
- Report slots persist before handoff to MeshService, favoring a missed message over duplicate broadcasts if power fails. A missed slot can retry for 30 minutes, then is skipped. A congested router or subsequent RF failure can still drop a report; the bot does not flood retries.

## Implementation and reliability

`WeatherBotModule` integrates with the cooperative Meshtastic scheduler. A separate low-priority ESP32 task performs HTTPS and parsing; bounded queues return results to the main thread, which alone allocates and sends mesh packets. Downloads have connection/read/handshake deadlines and a 64 KiB body cap. JSON allocations use PSRAM. Malformed, stale or incomplete forecasts are rejected. Network/parser failures back off to 5, 10 and 15 minutes and remain silent on LoRa.

TLS verifies server chains and names using embedded public roots (USERTrust RSA and ISRG Root X1). A future CA change may require updating `WeatherBotTrust.h`; TLS verification must not be disabled. Normal Meshtastic network settings, NTP, timezone, channel selection and `MeshService::sendToMesh` are reused. There is no separate password system.

Stock Meshtastic 2.7.23 gates Bluetooth off when Wi-Fi is enabled. Only the weather build removes that gate on ESP32-S3 and enables Wi-Fi modem sleep for the SDK's built-in Wi-Fi/BLE coexistence. These changes do not affect the stock build. See `VALIDATION.md` for observed hardware results and remaining limits.

## Local controls

Install the Meshtastic Python package, then from this repository:

```powershell
python tools/weather_control.py status --port COM3 --node !YOUR_NODE_ID
python tools/weather_control.py preview --port COM3 --node !YOUR_NODE_ID
python tools/weather_control.py off --port COM3 --node !YOUR_NODE_ID
python tools/weather_control.py on --port COM3 --node !YOUR_NODE_ID
python tools/weather_control.py test --port COM3 --node !YOUR_NODE_ID
```

The helper verifies the node ID you supply and the weather build. Commands target the node itself on `PRIVATE_APP`; they are accepted only with the local-client source address. They do not travel over LoRa. `preview` logs a forecast only. `test` fetches real weather and sends one clearly marked `TEST:` forecast (or a short online message if the prefix would exceed the payload budget), persistently limited to once for this device. Repeated test commands and normal reboots cannot repeat it. `off` persists across reboots and retains ordinary Meshtastic functionality. Disabling Wi-Fi in the app also stops weather activity. Diagnostic API logging must be enabled to see output.

Ordinary serial clients can restart this ESP32-S3 on Windows. This helper holds USB reset-control lines inactive and waits 35 seconds so it also works just after a cold start. Weather waits for valid network time; Meshtastic retries a failed initial NTP request at its normal five-minute network check. Use `--wait 360` to observe that retry after a cold start.

Do not erase NVS simply to repeat the commissioning message. Internal mock alerts and scheduler/parser tests run without radio output; they never generate fake public warnings.

## Configuration and rebuilding

Wi-Fi credentials live in Meshtastic's normal `network.wifi_ssid`, `network.wifi_psk` and `network.wifi_enabled` settings, configurable from the app or local API. Neither credentials nor channel/private keys belong in this repository. Bluetooth remains controlled by `bluetooth.enabled`.

Report hours, location and feed URLs are in `src/modules/WeatherBotConfig.h`. Set Meshtastic's `device.tzdef` to `EST5EDT,M3.2.0/2,M11.1.0/2` for London. When changing locations, change coordinates, label, URL-encoded IANA timezone, POSIX timezone and official alert feeds together. Scheduled operation pauses if the device timezone differs from the configured bot timezone. Morning/evening formatting assumes morning before noon and evening after noon; the defaults are 7 and 19.

```powershell
git submodule update --init --recursive
python -m pip install platformio
python -m platformio run -e heltec-v4-weather
```

On Windows use a short project path or a drive mapping to avoid dependency path-length failures. The tested Windows build used `M:` mapped to the repository, with `PLATFORMIO_LIBDEPS_DIR=M:\.pio\libdeps` and `PLATFORMIO_BUILD_DIR=M:\.pio\build`. Build one environment at a time. Toolchain and library versions are pinned by the original 2.7.23 configuration and the added weather environment. To build stock 2.7.23, use `-e heltec-v4`.

Run the production parser/scheduler tests on the computer after dependencies are installed:

```powershell
python -m pip install ziglang
python tools/test_weather_core.py
```

These tests use captured public forecast/alert fixtures and never link radio code. The firmware also runs internal tests at startup and disables only the weather module if a check fails.

The custom application is `.pio/build/heltec-v4-weather/firmware-heltec-v4-weather-2.7.23.b246bcd.bin`. After verifying the same device and partition described below, flash that application with the same application-only command used for recovery, substituting its path. Do not flash the generated factory image for a routine update.

## Recovery

Keep a private configuration export and full flash backup before installing custom firmware. Never publish them: they contain identity/channel keys and may include Wi-Fi credentials.

Official stock images are available from the [Meshtastic 2.7.23 release](https://github.com/meshtastic/firmware/releases/tag/v2.7.23.b246bcd). Use the `heltec-v4` application for the tested board, not `heltec-v4-tft` or `heltec-v4-r8`.

On the commissioning board, the verified active app0 offset was `0x10000`, size `0x640000`. After independently verifying your port, hardware and active partition, an application-only restore has this form:

```powershell
python -m esptool --chip esp32s3 --port COM3 --baud 921600 write-flash 0x10000 firmware-heltec-v4-2.7.23.b246bcd.bin
```

Use your own port and verified offset. An application-only write preserves the configuration partitions; a factory-image write is not an ordinary application update. Full-flash recovery must use your own private backup. Stock 2.7.23 disables Bluetooth while Wi-Fi is enabled; use USB to disable Wi-Fi if restoring stock and wanting Bluetooth.
