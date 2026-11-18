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
    String _nwsForecastUrl;
    String _nwsHourlyUrl;
    String _nwsStationsUrl;
    String _nwsLatestObsUrl;

    bool httpRequest(const String& url, const char* method, const String& body, String& responseOut, int timeoutMs = 1800, size_t maxBytes = 0);
    bool ensureNwsEndpoints();
    bool fetchNwsDailyForecast(float& outHigh, float& outLow, int& outPrecip, float& outFallbackTemp, int& outFallbackCode);
    bool fetchNwsHourlyCurrent(float& outTemp, int& outCode);
    bool fetchNwsStationObservation(float& outTemp, int& outCode, float& outHigh24, float& outLow24, bool& outHasHiLo24);
};
