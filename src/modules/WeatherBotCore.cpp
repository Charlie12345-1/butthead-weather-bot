#include "configuration.h"
#if defined(BUTTHEAD_WEATHER_BOT)
#include "RTC.h"
#include "WeatherBotCore.h"
#include <ArduinoJson.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include <memory>

namespace weatherbot
{
Schedule schedule(time_t now, uint32_t lastMorning, uint32_t lastEvening)
{
    Schedule result;
    tm local = {};
    localtime_r(&now, &local);
    uint32_t day = (local.tm_year + 1900) * 10000 + (local.tm_mon + 1) * 100 + local.tm_mday;
    for (int offset = 0; offset <= 1; ++offset) {
        for (int slot = 0; slot < 2; ++slot) {
            tm target = local;
            target.tm_mday += offset;
            target.tm_hour = slot ? EVENING_HOUR : MORNING_HOUR;
            target.tm_min = target.tm_sec = 0;
            target.tm_isdst = -1;
            time_t when = mktime(&target);
            uint32_t key = day * 2 + slot;
            uint32_t previous = slot ? lastEvening : lastMorning;
            if (!offset && now >= when && now - when < REPORT_GRACE_SECONDS && key > previous) {
                result.dueKey = key;
                result.evening = slot != 0;
            }
            if (when > now && (!result.next || when < result.next))
                result.next = when;
        }
    }
    return result;
}

uint64_t fingerprint(const std::string &value)
{
    uint64_t hash = 14695981039346656037ULL;
    for (unsigned char c : value) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash ? hash : 1;
}

std::string shorten(const std::string &text, size_t limit)
{
    if (text.size() <= limit)
        return text;
    if (limit < 4)
        return "";
    size_t end = limit - 3;
    while (end && (static_cast<unsigned char>(text[end]) & 0xc0) == 0x80)
        --end;
    auto space = text.rfind(' ', end);
    if (space != std::string::npos && space > end / 2)
        end = space;
    return text.substr(0, end) + "...";
}

std::string plainText(const char *text)
{
    std::string out;
    bool space = false;
    if (!text)
        return out;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(text); *p; ++p) {
        if (*p <= 32)
            space = true;
        else {
            if (space && !out.empty())
                out += ' ';
            out += static_cast<char>(*p);
            space = false;
        }
    }
    return out;
}

struct JsonAllocator {
    void *allocate(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
    void deallocate(void *pointer) { heap_caps_free(pointer); }
    void *reallocate(void *pointer, size_t size) { return heap_caps_realloc(pointer, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
};

static time_t isoUtc(const char *value)
{
    if (!value || strlen(value) < 20 || strlen(value) > 32 || value[strlen(value) - 1] != 'Z')
        return 0;
    tm t = {};
    int year, month, day, hour, minute, second;
    if (sscanf(value, "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6 || year < 2020 || year > 2099 ||
        month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 ||
        second > 59)
        return 0;
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min = minute;
    t.tm_sec = second;
    time_t result = gm_mktime(&t);
    tm check = {};
    gmtime_r(&result, &check);
    return check.tm_year == t.tm_year && check.tm_mon == t.tm_mon && check.tm_mday == t.tm_mday ? result : 0;
}

bool parseAlerts(const std::string &json, unsigned feed, time_t now, AlertSet &out)
{
    out = {};
    if (feed > 1 || json.size() > 65536)
        return false;
    BasicJsonDocument<JsonAllocator> doc(56000);
    if (deserializeJson(doc, json) || doc.overflowed() || strcmp(doc["type"] | "", "FeatureCollection") ||
        !doc["features"].is<JsonArray>() || !doc["numberMatched"].is<unsigned>() || !doc["numberReturned"].is<unsigned>())
        return false;
    JsonArrayConst features = doc["features"];
    unsigned total = doc["numberMatched"];
    if (total > MAX_ALERTS || total != features.size() || doc["numberReturned"].as<unsigned>() != total)
        return false;
    time_t timestamp = isoUtc(doc["timeStamp"] | "");
    if (!timestamp || timestamp > now + 300 || now - timestamp > 1800)
        return false;
    for (JsonObjectConst feature : features) {
        JsonObjectConst p = feature["properties"];
        if (strcmp(p["feature_name_en"] | "", FEED_TITLES[feed]))
            return false;
        std::string status = p["status_en"] | "";
        if (status == "ended" || status == "cancelled")
            continue;
        time_t expiry = isoUtc(p["expiration_datetime"] | "");
        time_t issued = isoUtc(p["publication_datetime"] | "");
        if (status.empty() || !expiry || !issued || issued > now + 300)
            return false;
        if (expiry <= now)
            continue;
        std::string event = plainText(p["alert_name_en"] | "");
        std::string summary = plainText(p["alert_text_en"] | "");
        std::string id = feature["id"] | "";
        std::string colour = plainText(p["risk_colour_en"] | "");
        if (event.empty() || summary.empty() || id.empty())
            return false;
        Alert &alert = out.alerts[out.count++];
        alert.identifier = fingerprint(id + (p["publication_datetime"] | ""));
        // Ignore issue/expiry timestamp churn, but retain official severity and full bulletin content.
        alert.fingerprint =
            fingerprint(event + "\n" + summary + "\n" + colour + "\n" + (p["impact_en"] | "") + "\n" + (p["confidence_en"] | ""));
        std::string text = "WEATHER ALERT: " + colour + " " + event + " - " + ALERT_AREAS[feed] + ". ECCC: " + summary;
        text = shorten(text, MAX_MESSAGE_BYTES);
        memcpy(alert.text, text.c_str(), text.size() + 1);
    }
    return true;
}

static const char *conditions(int code)
{
    if (code == 0)
        return "clear";
    if (code <= 2)
        return "partly cloudy";
    if (code == 3)
        return "overcast";
    if (code == 45 || code == 48)
        return "fog";
    if (code == 56 || code == 57 || code == 66 || code == 67)
        return "freezing rain";
    if (code >= 51 && code <= 55)
        return "drizzle";
    if (code >= 61 && code <= 65)
        return "rain";
    if (code >= 71 && code <= 77)
        return "snow";
    if (code >= 80 && code <= 82)
        return "showers";
    if (code == 85 || code == 86)
        return "snow showers";
    if (code >= 95 && code <= 99)
        return "thunderstorms";
    return "mixed weather";
}

static const char *direction(float degrees)
{
    static const char *names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    return names[static_cast<int>(std::round(degrees / 45)) % 8];
}

static bool number(JsonVariantConst value, float min, float max)
{
    return value.is<float>() && std::isfinite(value.as<float>()) && value.as<float>() >= min && value.as<float>() <= max;
}

bool parseForecast(const std::string &json, time_t now, bool evening, std::string &out)
{
    BasicJsonDocument<JsonAllocator> doc(40000);
    if (deserializeJson(doc, json) || doc.overflowed())
        return false;
    JsonObjectConst current = doc["current"], daily = doc["daily"], hourly = doc["hourly"];
    tm local = {};
    localtime_r(&now, &local);
    char today[16], tomorrow[16];
    strftime(today, sizeof(today), "%Y-%m-%d", &local);
    tm next = local;
    next.tm_mday++;
    next.tm_hour = 12;
    next.tm_isdst = -1;
    mktime(&next);
    strftime(tomorrow, sizeof(tomorrow), "%Y-%m-%d", &next);
    if (strcmp(daily["time"][0] | "", today) || strcmp(daily["time"][1] | "", tomorrow))
        return false;
    const char *currentTime = current["time"] | "";
    if (strlen(currentTime) < 16 || strncmp(currentTime, today, 10))
        return false;
    int hour = -1, minute = -1;
    if (sscanf(currentTime + 11, "%d:%d", &hour, &minute) != 2 || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        std::abs(hour * 60 + minute - local.tm_hour * 60 - local.tm_min) > 90)
        return false;
    for (const char *name : {"temperature_2m", "apparent_temperature"})
        if (!number(current[name], -90, 65))
            return false;
    if (!number(current["weather_code"], 0, 99) || !number(current["wind_speed_10m"], 0, 400) ||
        !number(current["wind_direction_10m"], 0, 360))
        return false;
    for (int d = 0; d < 2; ++d) {
        for (const char *name : {"temperature_2m_max", "temperature_2m_min"})
            if (!number(daily[name][d], -90, 65))
                return false;
        if (!number(daily["weather_code"][d], 0, 99) || !number(daily["precipitation_probability_max"][d], 0, 100) ||
            !number(daily["rain_sum"][d], 0, 1000) || !number(daily["snowfall_sum"][d], 0, 200) ||
            !number(daily["wind_speed_10m_max"][d], 0, 400) || !number(daily["wind_direction_10m_dominant"][d], 0, 360))
            return false;
    }
    char text[512], extra[80] = {};
    if (!evening) {
        if (daily["snowfall_sum"][0].as<float>() >= 0.5)
            snprintf(extra, sizeof(extra), ", snow %.1fcm", daily["snowfall_sum"][0].as<float>());
        else if (daily["rain_sum"][0].as<float>() >= 1)
            snprintf(extra, sizeof(extra), ", rain %.0fmm", daily["rain_sum"][0].as<float>());
        snprintf(text, sizeof(text),
                 "%s %dAM: %.0fC feels %.0f, %s. Today %.0f/%.0fC %s, precip "
                 "%d%%%s; %s %.0fkm/h. Tomorrow %.0f/%.0fC %s %d%%.",
                 LOCATION, MORNING_HOUR, current["temperature_2m"].as<float>(), current["apparent_temperature"].as<float>(),
                 conditions(current["weather_code"]), daily["temperature_2m_max"][0].as<float>(),
                 daily["temperature_2m_min"][0].as<float>(), conditions(daily["weather_code"][0]),
                 daily["precipitation_probability_max"][0].as<int>(), extra, direction(current["wind_direction_10m"]),
                 current["wind_speed_10m"].as<float>(), daily["temperature_2m_max"][1].as<float>(),
                 daily["temperature_2m_min"][1].as<float>(), conditions(daily["weather_code"][1]),
                 daily["precipitation_probability_max"][1].as<int>());
    } else {
        float low = 100, rain = 0, snow = 0, wind = 0;
        unsigned samples = 0;
        JsonArrayConst times = hourly["time"];
        for (unsigned h = 0; h < times.size(); ++h) {
            const char *stamp = times[h] | "";
            if (strlen(stamp) != 16)
                return false;
            int hh = atoi(stamp + 11);
            if ((!strncmp(stamp, today, 10) && hh >= EVENING_HOUR) || (!strncmp(stamp, tomorrow, 10) && hh < MORNING_HOUR)) {
                if (!number(hourly["temperature_2m"][h], -90, 65) || !number(hourly["rain"][h], 0, 500) ||
                    !number(hourly["snowfall"][h], 0, 100) || !number(hourly["wind_speed_10m"][h], 0, 400))
                    return false;
                low = std::min(low, hourly["temperature_2m"][h].as<float>());
                wind = std::max(wind, hourly["wind_speed_10m"][h].as<float>());
                rain += hourly["rain"][h].as<float>();
                snow += hourly["snowfall"][h].as<float>();
                ++samples;
            }
        }
        constexpr unsigned nightHours = 24 - EVENING_HOUR + MORNING_HOUR;
        if (samples < nightHours - 1 || samples > nightHours + 1)
            return false;
        std::string night;
        if (snow >= 0.5) {
            snprintf(extra, sizeof(extra), " snow %.1fcm", snow);
            night += extra;
        } else if (rain >= 1) {
            snprintf(extra, sizeof(extra), " rain %.0fmm", rain);
            night += extra;
        }
        if (wind >= 25) {
            snprintf(extra, sizeof(extra), " wind %.0fkm/h", wind);
            night += extra;
        }
        std::string tomorrowWind;
        if (daily["wind_speed_10m_max"][1].as<float>() >= 25) {
            snprintf(extra, sizeof(extra), ", %s %.0fkm/h", direction(daily["wind_direction_10m_dominant"][1]),
                     daily["wind_speed_10m_max"][1].as<float>());
            tomorrowWind = extra;
        }
        snprintf(text, sizeof(text),
                 "%s %dPM: %.0fC %s. Overnight %.0fC%s. Tomorrow %.0f/%.0fC %s, "
                 "precip %d%%%s.",
                 LOCATION, EVENING_HOUR - 12, current["temperature_2m"].as<float>(), conditions(current["weather_code"]), low,
                 night.c_str(), daily["temperature_2m_max"][1].as<float>(), daily["temperature_2m_min"][1].as<float>(),
                 conditions(daily["weather_code"][1]), daily["precipitation_probability_max"][1].as<int>(), tomorrowWind.c_str());
    }
    // Never truncate away tomorrow's forecast; an oversized report is skipped
    // instead.
    out = text;
    return out.size() <= MAX_MESSAGE_BYTES;
}

bool selfTest()
{
    BasicJsonDocument<JsonAllocator> alert(6000);
    alert["type"] = "FeatureCollection";
    alert["numberMatched"] = 1;
    alert["numberReturned"] = 1;
    alert["timeStamp"] = "2026-09-28T11:00:00Z";
    alert["features"][0]["id"] = "mock-one";
    auto properties = alert["features"][0]["properties"];
    properties["feature_name_en"] = FEED_TITLES[0];
    properties["status_en"] = "issued";
    properties["publication_datetime"] = "2026-09-28T10:30:00Z";
    properties["expiration_datetime"] = "2026-09-29T00:00:00Z";
    properties["alert_name_en"] = "severe thunderstorm warning";
    properties["risk_colour_en"] = "yellow";
    properties["alert_text_en"] = "Damaging winds possible.";
    auto parsed = std::unique_ptr<AlertSet>(new AlertSet);
    std::string payload;
    serializeJson(alert, payload);
    if (!parseAlerts(payload, 0, 1790593200, *parsed) || parsed->count != 1)
        return false;
    uint64_t original = parsed->alerts[0].fingerprint;
    properties["publication_datetime"] = "2026-09-28T10:45:00Z";
    alert["features"][0]["id"] = "mock-two";
    payload.clear();
    serializeJson(alert, payload);
    if (!parseAlerts(payload, 0, 1790593200, *parsed) || parsed->alerts[0].fingerprint != original)
        return false;
    properties["alert_text_en"] = "Destructive winds possible.";
    payload.clear();
    serializeJson(alert, payload);
    if (!parseAlerts(payload, 0, 1790593200, *parsed) || parsed->alerts[0].fingerprint == original)
        return false;
    properties["status_en"] = "ended";
    payload.clear();
    serializeJson(alert, payload);
    if (!parseAlerts(payload, 0, 1790593200, *parsed) || parsed->count)
        return false;
    if (parseAlerts("{bad", 0, 1790593200, *parsed) || parseAlerts(payload, 1, 1790593200, *parsed) ||
        parseAlerts(payload, 0, 1790636400, *parsed))
        return false;
    alert["numberMatched"] = 2;
    payload.clear();
    serializeJson(alert, payload);
    if (parseAlerts(payload, 0, 1790593200, *parsed))
        return false;
    if (shorten(std::string(300, 'x'), MAX_MESSAGE_BYTES).size() > MAX_MESSAGE_BYTES)
        return false;
    // UTC reference instants exercise both daylight-saving transitions and reboot
    // suppression.
    if (MORNING_HOUR == 7 && EVENING_HOUR == 19 && !strcmp(POSIX_TZ, "EST5EDT,M3.2.0/2,M11.1.0/2") &&
        (schedule(1772928000, 0, 0).next != 1772967600 || schedule(1793487600, 0, 0).next != 1793534400 ||
         schedule(1790593199, 0, 0).dueKey || schedule(1790593200, 0, 0).dueKey != 20260928 * 2 ||
         schedule(1790593200, 20260928 * 2, 0).dueKey || schedule(1790595000, 0, 0).dueKey ||
         schedule(1790636400, 0, 0).dueKey != 20260928 * 2 + 1))
        return false;
    tm testLocal = {};
    testLocal.tm_year = 126;
    testLocal.tm_mon = 8;
    testLocal.tm_mday = 28;
    testLocal.tm_hour = 7;
    testLocal.tm_isdst = -1;
    time_t morningNow = mktime(&testLocal);
    testLocal.tm_hour = 19;
    testLocal.tm_isdst = -1;
    time_t eveningNow = mktime(&testLocal);
    BasicJsonDocument<JsonAllocator> sample(24000);
    sample["current"]["time"] = "2026-09-28T19:00";
    sample["current"]["temperature_2m"] = 14;
    sample["current"]["apparent_temperature"] = 13;
    sample["current"]["weather_code"] = 3;
    sample["current"]["wind_speed_10m"] = 15;
    sample["current"]["wind_direction_10m"] = 270;
    for (int day = 0; day < 2; ++day) {
        sample["daily"]["time"][day] = day ? "2026-09-29" : "2026-09-28";
        sample["daily"]["temperature_2m_max"][day] = 18;
        sample["daily"]["temperature_2m_min"][day] = 8;
        sample["daily"]["weather_code"][day] = 61;
        sample["daily"]["precipitation_probability_max"][day] = 60;
        sample["daily"]["rain_sum"][day] = 3;
        sample["daily"]["snowfall_sum"][day] = 0;
        sample["daily"]["wind_speed_10m_max"][day] = 30;
        sample["daily"]["wind_direction_10m_dominant"][day] = 225;
    }
    for (int h = 0; h < 48; ++h) {
        char stamp[20];
        snprintf(stamp, sizeof(stamp), "2026-09-%02dT%02d:00", 28 + h / 24, h % 24);
        sample["hourly"]["time"][h] = std::string(stamp);
        sample["hourly"]["temperature_2m"][h] = h == 24 ? 7 : 10;
        sample["hourly"]["rain"][h] = 0.2;
        sample["hourly"]["snowfall"][h] = 0;
        sample["hourly"]["wind_speed_10m"][h] = 30;
    }
    std::string json, report;
    serializeJson(sample, json);
    if (!parseForecast(json, eveningNow, true, report) || report.find("Overnight 7C") == std::string::npos ||
        report.find("Tomorrow") == std::string::npos)
        return false;
    sample["current"]["time"] = "2026-09-28T07:00";
    json.clear();
    serializeJson(sample, json);
    if (!parseForecast(json, morningNow, false, report) || report.find("Tomorrow") == std::string::npos ||
        parseForecast(json, eveningNow, false, report))
        return false;
    sample["current"]["temperature_2m"] = nullptr;
    json.clear();
    serializeJson(sample, json);
    return !parseForecast(json, morningNow, false, report) && !parseForecast("{bad", morningNow, false, report);
}
} // namespace weatherbot
#endif
