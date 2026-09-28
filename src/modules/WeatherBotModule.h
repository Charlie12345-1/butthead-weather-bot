#pragma once
#include "configuration.h"
#if defined(BUTTHEAD_WEATHER_BOT)
#include "SinglePortModule.h"
#include "WeatherBotCore.h"
#include "concurrency/OSThread.h"
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class WeatherBotModule : public SinglePortModule, public concurrency::OSThread
{
  public:
    WeatherBotModule();

  protected:
    int32_t runOnce() override;
    bool wantPacket(const meshtastic_MeshPacket *p) override;
    ProcessMessage handleReceived(const meshtastic_MeshPacket &p) override;

  private:
    enum JobKind : uint8_t { Preview, Morning, Evening, Test, AlertsEast, AlertsWest };
    struct Job {
        JobKind kind;
        time_t now;
        uint32_t key;
    };
    struct Result {
        Job job = {};
        bool ok = false;
        int httpStatus = 0;
        char text[weatherbot::MAX_MESSAGE_BYTES + 1] = {};
        weatherbot::AlertSet alerts = {};
        uint32_t stackFree = 0;
    };
    struct Seen {
        uint64_t fingerprint = 0;
        uint64_t identifier = 0;
        bool announced = false;
    };
    struct AlertState {
        uint32_t version = 1;
        unsigned count = 0;
        Seen seen[weatherbot::MAX_ALERTS] = {};
    };
    Preferences prefs;
    QueueHandle_t requests = nullptr, responses = nullptr;
    bool ready = false, enabled = true, busy = false, previewPending = true, testPending = false;
    uint32_t lastMorning = 0, lastEvening = 0, lastSent = 0;
    uint32_t lastAttempt[6] = {}, retryMs[6] = {};
    bool attempted[6] = {};
    time_t nextLogged = 0;
    AlertState states[2] = {};
    Result result;
    bool pendingAlerts = false;
    unsigned pendingIndex = 0;
    static void worker(void *context);
    bool dispatch(JobKind kind, time_t now, uint32_t key = 0);
    bool canSend() const;
    void sendText(const char *text);
    void acceptResult();
    void saveAlerts(unsigned feed);
};
#endif
