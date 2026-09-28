#include "configuration.h"
#if defined(BUTTHEAD_WEATHER_BOT)
#include "LoRaFEMInterface.h"
#include "MeshService.h"
#include "RTC.h"
#include "Throttle.h"
#include "WeatherBotModule.h"
#include "WeatherBotTrust.h"
#include "airtime.h"
#include "main.h"
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <algorithm>

using namespace weatherbot;

namespace
{
class BoundedBody : public Stream
{
  public:
    std::string data;
    bool failed = false;
    BoundedBody() : started(millis()) { data.reserve(4096); }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
    size_t write(uint8_t byte) override { return write(&byte, 1); }
    size_t write(const uint8_t *bytes, size_t size) override
    {
        if (data.size() + size > 65536 || !Throttle::isWithinTimespanMs(started, 20000)) {
            failed = true;
            return 0;
        }
        data.append(reinterpret_cast<const char *>(bytes), size);
        return size;
    }

  private:
    uint32_t started;
};

bool fetch(const std::string &url, std::string &body, int &status)
{
    WiFiClientSecure tls;
    tls.setCACert(WEATHER_ROOTS);
    tls.setHandshakeTimeout(8);
    tls.setTimeout(8000);
    HTTPClient http;
    http.setConnectTimeout(5000);
    http.setTimeout(8000);
    http.setReuse(false);
    http.useHTTP10(true);
    http.setUserAgent("Butthead-Meshtastic-Weather/1.0");
    if (!http.begin(tls, url.c_str()))
        return false;
    status = http.GET();
    if (status != HTTP_CODE_OK || http.getSize() > 65536) {
        http.end();
        return false;
    }
    BoundedBody sink;
    int count = http.writeToStream(&sink);
    http.end();
    if (count <= 0 || sink.failed)
        return false;
    body.swap(sink.data);
    return true;
}

std::string forecastUrl()
{
    return std::string("https://api.open-meteo.com/v1/forecast?latitude=") + LATITUDE + "&longitude=" + LONGITUDE +
           "&timezone=" + TIMEZONE +
           "&forecast_days=2&current=temperature_2m,apparent_temperature,weather_"
           "code,wind_speed_10m,wind_direction_10m"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
           "precipitation_probability_max,rain_sum,snowfall_sum,wind_speed_10m_"
           "max,wind_direction_10m_dominant"
           "&hourly=temperature_2m,rain,snowfall,wind_speed_10m";
}
} // namespace

WeatherBotModule::WeatherBotModule() : SinglePortModule("weather", meshtastic_PortNum_PRIVATE_APP), OSThread("WeatherBot")
{
    setIntervalFromNow(30000);
}

bool WeatherBotModule::wantPacket(const meshtastic_MeshPacket *p)
{
    return p->decoded.portnum == meshtastic_PortNum_PRIVATE_APP && p->from == 0 && p->to == nodeDB->getNodeNum();
}

ProcessMessage WeatherBotModule::handleReceived(const meshtastic_MeshPacket &p)
{
    if (!ready)
        return ProcessMessage::STOP;
    std::string command(reinterpret_cast<const char *>(p.decoded.payload.bytes), p.decoded.payload.size);
    if (command == "weather:off" || command == "weather:on") {
        enabled = command == "weather:on";
        prefs.putBool("enabled", enabled);
        LOG_INFO("Weather bot enabled=%d", enabled);
    } else if (command == "weather:preview") {
        previewPending = true;
        attempted[Preview] = false;
    } else if (command == "weather:test" && !prefs.getBool("tested", false)) {
        testPending = true;
        attempted[Test] = false;
        LOG_INFO("Weather one-time test requested locally");
    } else if (command == "weather:test") {
        LOG_INFO("Weather one-time test already completed; duplicate suppressed");
    } else if (command == "weather:status") {
        LOG_INFO("Weather enabled=%d wifi=%d test_done=%d heap=%u min_heap=%u", enabled, WiFi.isConnected(),
                 prefs.getBool("tested", false), ESP.getFreeHeap(), ESP.getMinFreeHeap());
        previewPending = true;
        attempted[Preview] = false;
    }
    return ProcessMessage::STOP;
}

void WeatherBotModule::worker(void *context)
{
    auto *self = static_cast<WeatherBotModule *>(context);
    Job job;
    auto *reply = new Result;
    for (;;) {
        if (xQueueReceive(self->requests, &job, portMAX_DELAY) != pdTRUE)
            continue;
        *reply = {};
        reply->job = job;
        if (WiFi.isConnected()) {
            std::string body;
            if (job.kind >= AlertsEast) {
                unsigned feed = job.kind - AlertsEast;
                reply->ok = fetch(ALERT_URLS[feed], body, reply->httpStatus) && parseAlerts(body, feed, job.now, reply->alerts);
            } else {
                std::string message;
                reply->ok =
                    fetch(forecastUrl(), body, reply->httpStatus) && parseForecast(body, job.now, job.kind == Evening, message);
                if (reply->ok)
                    memcpy(reply->text, message.c_str(), message.size() + 1);
            }
        }
        reply->stackFree = uxTaskGetStackHighWaterMark(nullptr);
        xQueueSend(self->responses, reply, portMAX_DELAY);
    }
}

bool WeatherBotModule::dispatch(JobKind kind, time_t now, uint32_t key)
{
    if (attempted[kind] && Throttle::isWithinTimespanMs(lastAttempt[kind], retryMs[kind]))
        return false;
    Job job{kind, now, key};
    if (xQueueSend(requests, &job, 0) != pdTRUE)
        return false;
    lastAttempt[kind] = millis();
    attempted[kind] = true;
    busy = true;
    return true;
}

bool WeatherBotModule::canSend() const
{
    return config.lora.tx_enabled && config.lora.use_preset &&
           config.lora.modem_preset == meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST &&
           (!lastSent || !Throttle::isWithinTimespanMs(lastSent, MIN_SEND_INTERVAL_MS)) && router->getQueueStatus().free > 0 &&
           airTime->isTxAllowedChannelUtil(true) && airTime->isTxAllowedAirUtil();
}

void WeatherBotModule::sendText(const char *text)
{
    auto *packet = allocDataPacket();
    packet->to = NODENUM_BROADCAST;
    packet->channel = channels.getPrimaryIndex();
    packet->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
    packet->decoded.want_response = false;
    packet->want_ack = false;
    packet->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
    packet->decoded.payload.size = strlen(text);
    memcpy(packet->decoded.payload.bytes, text, packet->decoded.payload.size);
    // Limit only this bot's reach; preserve the user's normal hop-limit setting.
    packet->hop_limit = std::min<uint8_t>(config.lora.hop_limit, 3);
    lastSent = millis();
    LOG_INFO("Weather handing text to MeshService id=%u channel=%u bytes=%u", packet->id, packet->channel,
             packet->decoded.payload.size);
    LOG_INFO("Weather TX: %s", text);
    service->sendToMesh(packet, RX_SRC_LOCAL, true);
}

void WeatherBotModule::saveAlerts(unsigned feed)
{
    prefs.putBytes(feed ? "west" : "east", &states[feed], sizeof(AlertState));
}

void WeatherBotModule::acceptResult()
{
    busy = false;
    auto kind = result.job.kind;
    LOG_INFO("Weather job=%u success=%d HTTP=%d worker_stack_free=%u heap=%u", kind, result.ok, result.httpStatus,
             result.stackFree, ESP.getFreeHeap());
    if (!result.ok) {
        retryMs[kind] = retryMs[kind] ? std::min<uint32_t>(retryMs[kind] * 2, ALERT_INTERVAL_MS) : 5 * 60 * 1000;
        LOG_WARN("Weather download/parse unavailable; quiet retry in %u seconds", retryMs[kind] / 1000);
        return;
    }
    retryMs[kind] = 0;
    if (kind >= AlertsEast) {
        unsigned feed = kind - AlertsEast;
        retryMs[kind] = ALERT_INTERVAL_MS;
        AlertState next;
        next.count = result.alerts.count;
        for (unsigned n = 0; n < next.count; ++n) {
            auto &item = next.seen[n];
            item.fingerprint = result.alerts.alerts[n].fingerprint;
            item.identifier = result.alerts.alerts[n].identifier;
            for (unsigned previous = 0; previous < states[feed].count; ++previous)
                if (states[feed].seen[previous].fingerprint == item.fingerprint)
                    item.announced = states[feed].seen[previous].announced;
        }
        if (memcmp(&next, &states[feed], sizeof(next))) {
            states[feed] = next;
            saveAlerts(feed);
        }
        LOG_INFO("Weather ECCC %s valid snapshot; active=%u", ALERT_AREAS[feed], next.count);
        pendingAlerts = true;
        pendingIndex = 0;
        return;
    }
    LOG_INFO("Weather parsed (log only): %s", result.text);
    if (kind == Preview) {
        previewPending = false;
        return;
    }
    time_t now = getValidTime(RTCQualityFromNet);
    if (!enabled || !config.network.wifi_enabled || now < result.job.now || now - result.job.now > 300)
        return;
    if (!canSend()) {
        retryMs[kind] = 60 * 1000;
        return;
    }
    if (kind == Test) {
        testPending = false;
        if (!prefs.getBool("tested", false) && prefs.putBool("tested", true)) {
            std::string test = "TEST: " + std::string(result.text);
            sendText(test.size() <= MAX_MESSAGE_BYTES ? test.c_str() : "TEST: Butthead weather bot online.");
        }
    } else {
        Schedule slot = schedule(now, lastMorning, lastEvening);
        if (slot.dueKey != result.job.key)
            return;
        const char *key = kind == Morning ? "morning" : "evening";
        if (prefs.putUInt(key, result.job.key)) {
            (kind == Morning ? lastMorning : lastEvening) = result.job.key;
            sendText(result.text);
        }
    }
}

int32_t WeatherBotModule::runOnce()
{
    if (!ready) {
        if (!prefs.begin("weatherbot", false)) {
            LOG_ERROR("Weather preferences unavailable; bot disabled");
            return disable();
        }
        enabled = prefs.getBool("enabled", true);
        lastMorning = prefs.getUInt("morning", 0);
        lastEvening = prefs.getUInt("evening", 0);
        for (unsigned feed = 0; feed < 2; ++feed) {
            const char *key = feed ? "west" : "east";
            if (prefs.getBytesLength(key) == sizeof(AlertState))
                prefs.getBytes(key, &states[feed], sizeof(AlertState));
            if (states[feed].version != 1 || states[feed].count > MAX_ALERTS)
                states[feed] = {};
        }
        if (!selfTest()) {
            LOG_ERROR("Weather internal mock tests FAILED; bot disabled");
            return disable();
        }
        LOG_INFO("Weather self-tests PASS: new/duplicate/updated alerts, malformed "
                 "data, morning/evening forecasts, overnight low, DST, reboot "
                 "suppression; NO RF");
        requests = xQueueCreate(1, sizeof(Job));
        responses = xQueueCreate(1, sizeof(Result));
        if (!requests || !responses || xTaskCreatePinnedToCore(worker, "WeatherHTTP", 12288, this, 1, nullptr, 0) != pdPASS) {
            LOG_ERROR("Weather worker unavailable; bot disabled");
            return disable();
        }
        ready = true;
        LOG_INFO("Weather board: stock Heltec V4 front-end LNA control=%d (V4.3 KCT8103L expects 1)",
                 loraFEMInterface.isLnaCanControl());
        LOG_INFO("Weather ready: 07:00/19:00 America/Toronto; ECCC every 15 "
                 "minutes; enabled=%d",
                 enabled);
    }
    if (busy && xQueueReceive(responses, &result, 0) == pdTRUE)
        acceptResult();
    if (pendingAlerts) {
        unsigned feed = result.job.kind - AlertsEast;
        if (!enabled || !config.network.wifi_enabled || getValidTime(RTCQualityFromNet) - result.job.now > 15 * 60) {
            pendingAlerts = false;
        } else {
            while (pendingIndex < states[feed].count && states[feed].seen[pendingIndex].announced)
                ++pendingIndex;
            if (pendingIndex >= states[feed].count)
                pendingAlerts = false;
            else if (canSend()) {
                states[feed].seen[pendingIndex].announced = true;
                const char *key = feed ? "west" : "east";
                if (prefs.putBytes(key, &states[feed], sizeof(AlertState)) == sizeof(AlertState))
                    sendText(result.alerts.alerts[pendingIndex].text);
                ++pendingIndex;
            }
        }
    }
    time_t now = getValidTime(RTCQualityFromNet);
    if (!enabled || !config.network.wifi_enabled || !WiFi.isConnected() || now < 1700000000 || busy || pendingAlerts)
        return 1000;
    if (strcmp(config.device.tzdef, POSIX_TZ)) {
        LOG_WARN("Weather timezone differs from configured London zone; scheduled "
                 "reports paused");
        return 60000;
    }
    Schedule slot = schedule(now, lastMorning, lastEvening);
    if (slot.next != nextLogged) {
        nextLogged = slot.next;
        tm local = {};
        localtime_r(&nextLogged, &local);
        char stamp[48];
        strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M %Z", &local);
        LOG_INFO("Weather next routine report: %s; RTC=%s", stamp, RtcName(getRTCQuality()));
    }
    if (slot.dueKey && dispatch(slot.evening ? Evening : Morning, now, slot.dueKey))
        return 1000;
    if (testPending && dispatch(Test, now))
        return 1000;
    if (dispatch(AlertsEast, now))
        return 1000;
    if (dispatch(AlertsWest, now))
        return 1000;
    if (previewPending)
        dispatch(Preview, now);
    return 1000;
}
#endif
