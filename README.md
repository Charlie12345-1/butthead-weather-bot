# Butthead Weather Bot for Meshtastic

London, Ontario weather reports and official ECCC alerts from a real Meshtastic node.

This unofficial custom firmware extends **Meshtastic 2.7.23.b246bcd**, tested on one **Heltec WiFi LoRa 32 V4.3** with 16 MiB flash and 2 MiB PSRAM. It preserves the stock `heltec-v4` board support and adds the `heltec-v4-weather` build environment. It is not an official Meshtastic release and is not a substitute for official emergency notifications.

## Features

- Concise LongFast primary-channel forecasts at **7 AM and 7 PM America/Toronto**, including daylight saving and tomorrow's outlook.
- Official ECCC London/Eastern Middlesex and Western Middlesex alerts checked every **15 minutes**. New or changed alerts do not wait for a scheduled forecast.
- Persistent duplicate suppression, bounded messages, conservative airtime use, and quiet network-failure retries.
- Existing Meshtastic Wi-Fi configuration, normal LoRa messaging/routing, and Bluetooth/Wi-Fi coexistence on the tested ESP32-S3 board.
- Local USB preview, enable/disable, status, and a persistently limited one-time test command.

## Start here

- **[Set up weather for your own location](LOCAL-WEATHER-SETUP.md)** — coordinates, timezones, official alert districts, rebuilding and verification. The downloadable firmware is configured for London; changing Wi-Fi or GPS does not change that.
- [Configuration, build, controls and recovery](WEATHER-BOT-README.md)
- [Hardware validation and limitations](VALIDATION.md)
- [Original Meshtastic README](README-MESHTASTIC.md)
- [Official baseline source](https://github.com/meshtastic/firmware/tree/v2.7.23.b246bcd)

Clone this repository, initialize its submodules, then build:

```sh
git submodule update --init --recursive
python -m pip install platformio
python -m platformio run -e heltec-v4-weather
```

Use a short checkout path on Windows. Before flashing, identify your exact board and active partition and preserve a private backup of your configuration and flash. The tested board's application-only update used offset `0x10000`; do not assume that offset or the same target for other hardware.

The weather module is in `src/modules/WeatherBot*`. Report hours, coordinates, timezone and alert districts are in `WeatherBotConfig.h`. Wi-Fi credentials are entered through normal Meshtastic settings, never compiled into source. The local helper requires `--node` with your own node ID.

## Validation

The firmware was built, flashed and tested on the target V4.3. Wi-Fi, Bluetooth, official alert downloads, forecasts, LoRa receive/repeat traffic and reboot persistence were checked. One marked test forecast was transmitted and rebroadcast by a neighboring node. Parser/scheduler tests cover duplicates, updates, invalid data and DST without transmitting mock alerts. The actual 7 AM/7 PM deadlines were not awaited, and this is not a long-term uptime guarantee.

## Credits and license

Based on [Meshtastic firmware](https://github.com/meshtastic/firmware), retaining its [GPL-3.0 license](LICENSE) and upstream notices. Weather-bot additions are distributed under the same license. Meshtastic is a trademark of its respective owner; this project does not imply endorsement.

Forecast data: [Open-Meteo](https://open-meteo.com/), CC BY 4.0. Official alert data: [Environment and Climate Change Canada's MSC GeoMet](https://api.weather.gc.ca/collections/weather-alerts?f=html). Current forecast conditions are model-based. Fixture files contain public weather data, not device configuration.

Private flash dumps, Wi-Fi passwords, channel keys, identity keys, personal device identifiers and raw commissioning logs are not included.
