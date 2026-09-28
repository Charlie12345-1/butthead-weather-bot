#pragma once
#include "WeatherBotConfig.h"
#include <cstdint>
#include <ctime>
#include <string>

namespace weatherbot
{
struct Alert {
    uint64_t fingerprint = 0;
    uint64_t identifier = 0;
    char text[MAX_MESSAGE_BYTES + 1] = {};
};
struct AlertSet {
    unsigned count = 0;
    Alert alerts[MAX_ALERTS] = {};
};
struct Schedule {
    time_t next = 0;
    uint32_t dueKey = 0;
    bool evening = false;
};
Schedule schedule(time_t now, uint32_t lastMorning, uint32_t lastEvening);
uint64_t fingerprint(const std::string &value);
std::string plainText(const char *text);
std::string shorten(const std::string &text, size_t limit);
bool parseAlerts(const std::string &json, unsigned feed, time_t now, AlertSet &out);
bool parseForecast(const std::string &json, time_t now, bool evening, std::string &out);
bool selfTest();
} // namespace weatherbot
