# Set up weather for your own location

The downloadable v0.1.0 firmware is configured for **London, Ontario**. To use another location, edit the settings below, build your own firmware, and flash that build. Changing Wi-Fi, the node name, GPS position or the phone's location does **not** change the weather location. There is currently no location picker in the Meshtastic app for this module.

These instructions cover the tested Heltec V4.3 build. Other boards require their own compatible build target and validation; do not flash the V4 image onto a different board.

## 1. Get the source and choose your location

```sh
git clone --branch main --recurse-submodules https://github.com/Charlie12345-1/butthead-weather-bot.git
cd butthead-weather-bot
```

Open [`src/modules/WeatherBotConfig.h`](src/modules/WeatherBotConfig.h). Change these settings together:

| Setting | What to enter |
| --- | --- |
| `LATITUDE` | Your forecast point's decimal latitude, between -90 and 90. |
| `LONGITUDE` | Decimal longitude, between -180 and 180. West of Greenwich is negative. |
| `LOCATION` | A short place name that will appear in each message. |
| `TIMEZONE` | The location's IANA timezone, URL-encoded for Open-Meteo; `/` becomes `%2F`. |
| `POSIX_TZ` | The device's timezone rule, including the correct daylight-saving rules where applicable. This is a different format from the IANA name. |
| `MORNING_HOUR`, `EVENING_HOUR` | Local report hours in 24-hour time; keep 7 and 19 initially. |

Use a map or the [Open-Meteo forecast configuration page](https://open-meteo.com/en/docs) to choose the coordinates and IANA timezone. A city-centre forecast point is sufficient; you do not need to publish a home address. Keep the default Celsius/km/h units unless you also adapt the parser and message formatting.

### Example: Toronto forecast

Replace the corresponding declarations inside `namespace weatherbot` with:

```cpp
constexpr const char *LATITUDE = "43.6532";
constexpr const char *LONGITUDE = "-79.3832";
constexpr const char *LOCATION = "Toronto";
constexpr const char *TIMEZONE = "America%2FToronto";
constexpr const char *POSIX_TZ = "EST5EDT,M3.2.0/2,M11.1.0/2";
constexpr int MORNING_HOUR = 7;
constexpr int EVENING_HOUR = 19;
```

This changes the forecast only. You must also replace the London alert districts in the next step.

For a different timezone, obtain the correct POSIX rule for that location; do not simply substitute the current UTC offset. A fixed offset will not follow daylight saving. Keep the IANA timezone, POSIX rule and device setting consistent. Some locations have no daylight saving or have rules that differ from Toronto.

If changing report hours later, the current formatter expects a morning hour from 1–11 and an evening hour from 13–23. Other choices need formatting changes. The startup summary banner contains the original 07:00/19:00 wording; use the calculated `Weather next routine report` log to verify the actual schedule after changing hours.

## 2. Select your official Canadian alert districts

Forecast coordinates and alert districts are independent. The bot does not infer alert coverage from latitude/longitude.

Find the official district names in [ECCC's region directory](https://www.canada.ca/en/environment-climate-change/services/weather-general-tools-resources/weatheroffice-online-services/atom-feeds.html). That page supplies region names and ATOM links, but this bot uses the **GeoMet JSON API**, not those ATOM/XML URLs. Check the actual coverage of each district; a nearby district is not necessarily your district.

Update all three arrays in `WeatherBotConfig.h`, keeping the same ordering:

| Array | Meaning |
| --- | --- |
| `ALERT_URLS` | GeoMet query URLs whose `feature_name_en` filter contains each exact official district name, URL-encoded. Preserve the other query parameters. |
| `FEED_TITLES` | Those same official names, written normally, including punctuation and accents. The parser checks exact matches. |
| `ALERT_AREAS` | Short, readable labels for the two areas in broadcast messages. |

**The current implementation requires exactly two distinct districts.** Choose two areas you actually intend to monitor. Do not duplicate a district to fill the second slot: persistent state is separate for each slot and could produce duplicate broadcasts. Do not leave a London district behind or remove an array entry. Monitoring only one district, more than two, or forecasts without any alerts requires a code change to the module's fixed feed count and scheduling, followed by tests.

### Example: Toronto plus Mississauga/Brampton alerts

This example intentionally covers both areas; it is not Toronto-only coverage. Replace all three existing arrays with:

```cpp
constexpr const char *ALERT_URLS[] = {
    "https://api.weather.gc.ca/collections/weather-alerts/"
    "items?f=json&limit=8&skipGeometry=true&properties=alert_name_en,alert_text_en,alert_type,feature_name_en,feature_id,status_"
    "en,expiration_datetime,publication_datetime,risk_colour_en,impact_en,confidence_en&feature_name_en=City%20of%20Toronto",
    "https://api.weather.gc.ca/collections/weather-alerts/"
    "items?f=json&limit=8&skipGeometry=true&properties=alert_name_en,alert_text_en,alert_type,feature_name_en,feature_id,status_"
    "en,expiration_datetime,publication_datetime,risk_colour_en,impact_en,confidence_en&feature_name_en=Mississauga%20-%20Brampton",
};
constexpr const char *FEED_TITLES[] = {"City of Toronto", "Mississauga - Brampton"};
constexpr const char *ALERT_AREAS[] = {"Toronto", "Mississauga/Brampton"};
```

For other names, Python can encode the query value correctly:

```sh
python -c "from urllib.parse import quote; print(quote('Your exact official district name', safe=''))"
```

Open each complete URL in a browser. Expect a JSON `FeatureCollection`, a recent `timeStamp`, and matching `numberMatched`/`numberReturned` values. Any returned feature's `properties.feature_name_en` must match the intended official name. An empty `features` array is normal when there are no alerts, but **an empty response alone cannot prove a district name is valid**: a typo can also match nothing. Cross-check the official directory and validate a correctly scoped active-alert fixture when available. The bot rejects incomplete responses and more than eight entries per district.

### Outside Canada

Open-Meteo supports forecasts for many locations worldwide, but this firmware's official-alert integration is specifically for ECCC's Canadian JSON schema. A different country's warning service requires a compatible adapter, geographic filtering, TLS certificates as needed, and internal tests. Merely replacing the URL with another country's API will not work. This release is not a ready-to-use international alert bot or a configurable forecast-only build.

## 3. Build and install your customized firmware

```sh
python -m pip install platformio ziglang meshtastic esptool
python -m platformio run -e heltec-v4-weather
python tools/test_weather_core.py
```

Use a short checkout path on Windows. See the [build and recovery guide](WEATHER-BOT-README.md#configuration-and-rebuilding) for the drive-mapping approach used during commissioning.

The existing test fixtures and reference DST checks were written for London/Toronto. For another timezone or different report hours, adapt the host test timezone and add expected schedule results for that zone, including its DST transitions if applicable. The Windows host test currently sets `TZ=EST5EDT`, and the fixed Toronto DST assertions only run for the original timezone and hours. Passing unchanged tests is not proof that a new zone's schedule was validated. If you change the morning hour, also update the internal forecast self-test's morning timestamp to exercise that hour's overnight range correctly.

Back up your own device configuration and flash privately. Verify the board, serial port and active app partition, then flash the **application `.bin` from your new build**, not the London binary downloaded from Releases. Follow the [application-only recovery/update guidance](WEATHER-BOT-README.md#recovery). Do not factory-reset or overwrite channel keys to change locations.

Firmware filenames can include a different Git revision after committing your changes; use the application actually produced in `.pio/build/heltec-v4-weather/`.

## 4. Set Wi-Fi and the device timezone

Enter your 2.4 GHz Wi-Fi network name/password through the Meshtastic app's Network settings. Credentials stay in the node's configuration; do not put them in the source or a GitHub issue.

Set Meshtastic's `device.tzdef` to exactly the same text as `POSIX_TZ`. For the Toronto example, using your own serial port:

```powershell
python -m meshtastic --port COM3 --set device.tzdef "EST5EDT,M3.2.0/2,M11.1.0/2"
python -m meshtastic --port COM3 --set security.debug_log_api_enabled true
```

The bot pauses network work if the device timezone does not match its compiled timezone. It also waits for valid network time; the usual initial NTP retry can take about five minutes. Changing this setting on the device does not replace the need to rebuild with matching location settings.

## 5. Check your own location before leaving it unattended

Replace `COM3` and `!12345678` with your port and actual node ID. Use one API client at a time:

```powershell
python tools/weather_control.py status --port COM3 --node '!12345678' --wait 360
python tools/weather_control.py preview --port COM3 --node '!12345678' --wait 60
```

If you previously disabled the bot, run the `on` command first; that preference survives application updates. Enabling the bot enables genuine official-alert broadcasts as well as scheduled reports.

Check that:

- Wi-Fi and network time are available, and the internal self-tests pass.
- `Weather next routine report` is correct for your local date/time.
- The preview names your place and includes tomorrow's forecast.
- Both ECCC snapshots succeed for the intended districts.
- Normal LoRa and Bluetooth operation still work after a reboot.

`preview` is log-only and does not transmit a weather report. `test` can send one clearly marked commissioning message; its completed flag persists, so a device used for earlier testing may suppress it. Do not erase persistent state just to repeat that message, and never transmit fabricated warning fixtures. Moving to a new location does not reset completed report slots or the test flag; a routine report already sent for that local date/slot is not sent again.

If the wrong place or districts appear, disable the weather bot with `off`, correct the configuration, and rebuild. Ordinary Meshtastic functionality remains available while the bot is disabled.
