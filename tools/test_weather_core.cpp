#include "RTC.h"
#include "WeatherBotCore.h"
#include "configuration.h"
#include <ArduinoJson.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

static void require(bool condition, const char *name)
{
    if (!condition) {
        std::cerr << "FAIL: " << name << '\n';
        std::exit(1);
    }
    std::cout << "PASS: " << name << '\n';
}

static std::string read(const char *path)
{
    std::ifstream file(path);
    require(file.good(), "fixture readable");
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

static time_t fixtureTime(const char *value, bool utc)
{
    tm date = {};
    int year, month;
    require(sscanf(value, "%d-%d-%dT%d:%d:%d", &year, &month, &date.tm_mday, &date.tm_hour, &date.tm_min, &date.tm_sec) >= 5,
            "fixture timestamp");
    date.tm_year = year - 1900;
    date.tm_mon = month - 1;
    date.tm_isdst = -1;
    return utc ? gm_mktime(&date) : mktime(&date);
}

int main(int argc, char **argv)
{
#ifdef _WIN32
    _putenv("TZ=EST5EDT");
    _tzset();
#else
    setenv("TZ", weatherbot::POSIX_TZ, 1);
    tzset();
#endif
    require(weatherbot::selfTest(), "production self-tests: DST, schedule, reports, alert changes and invalid data");
    require(argc == 4, "forecast, empty London snapshot, and active non-London snapshot provided");
    std::string forecast = read(argv[1]), report;
    DynamicJsonDocument doc(56000);
    require(!deserializeJson(doc, forecast), "live forecast fixture JSON");
    time_t now = fixtureTime(doc["current"]["time"], false);
    require(weatherbot::parseForecast(forecast, now, false, report), "real morning forecast parses");
    std::cout << report << '\n';
    require(report.size() <= weatherbot::MAX_MESSAGE_BYTES && report.find("Tomorrow") != std::string::npos,
            "morning payload and tomorrow");
    require(weatherbot::parseForecast(forecast, now, true, report), "real evening forecast parses");
    std::cout << report << '\n';
    require(report.size() <= weatherbot::MAX_MESSAGE_BYTES && report.find("Overnight") != std::string::npos,
            "evening payload and overnight");
    auto empty = read(argv[2]);
    doc.clear();
    require(!deserializeJson(doc, empty), "live ECCC empty snapshot JSON");
    now = fixtureTime(doc["timeStamp"], true);
    weatherbot::AlertSet alerts;
    require(weatherbot::parseAlerts(empty, 0, now, alerts) && alerts.count == 0, "real London no-alert snapshot accepted");
    require(!weatherbot::parseAlerts(empty, 0, now + 3600, alerts), "stale snapshot rejected");
    auto active = read(argv[3]);
    doc.clear();
    require(!deserializeJson(doc, active), "live ECCC active snapshot JSON");
    now = fixtureTime(doc["timeStamp"], true);
    require(!weatherbot::parseAlerts(active, 0, now, alerts), "another district never accepted as London");
    // Geography is changed only in this host-only fixture; this executable has no radio code.
    doc["features"][0]["properties"]["feature_name_en"] = weatherbot::FEED_TITLES[0];
    active.clear();
    serializeJson(doc, active);
    require(weatherbot::parseAlerts(active, 0, now, alerts) && alerts.count == 1,
            "official active-alert schema accepted in isolated fixture");
    auto original = alerts.alerts[0].fingerprint;
    doc["features"][0]["id"] = "new-issue-id";
    active.clear();
    serializeJson(doc, active);
    require(weatherbot::parseAlerts(active, 0, now, alerts) && alerts.alerts[0].fingerprint == original,
            "identifier churn suppresses duplicate");
    doc["features"][0]["properties"]["risk_colour_en"] = "red";
    active.clear();
    serializeJson(doc, active);
    require(weatherbot::parseAlerts(active, 0, now, alerts) && alerts.alerts[0].fingerprint != original,
            "severity upgrade recognized");
    doc["features"][0]["properties"]["expiration_datetime"] = "2026-01-01T00:00:00Z";
    active.clear();
    serializeJson(doc, active);
    require(weatherbot::parseAlerts(active, 0, now, alerts) && alerts.count == 0, "expired bulletin suppressed");
    require(!weatherbot::parseAlerts("{invalid", 0, now, alerts), "malformed JSON rejected");
    std::cout << "All weather core tests passed. No radio code linked.\n";
}
