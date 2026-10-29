#pragma once
#include <Arduino.h>
#include "app_state.h"

class NetClient {
public:
    void begin();
    bool ensureWiFi();
    bool updateClock(AppState& state);

    bool fetchNasStatus(RadioProgramData& outRadio);
    bool sendTune(float freq, int program, RadioProgramData& outRadio);

    bool triggerMediaPlayPause(AppState& state);
    bool triggerMediaVolume(bool up);
    bool syncMediaState(AppState& state);

    bool fetchWeather(WeatherData& outWeather);

private:
    bool httpRequest(const String& url, const char* method, const String& body, String& responseOut, int timeoutMs = 1800);
};
