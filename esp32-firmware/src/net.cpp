#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

void NetClient::begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    unsigned long startMs = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startMs) < 8000) {
        delay(200);
    }

    configTzTime(TZ_INFO, NTP_SERVER);
}

bool NetClient::ensureWiFi() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }
    WiFi.reconnect();
    return false;
}

bool NetClient::updateClock(AppState& state) {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 50)) {
        return false;
    }

    char timeBuf[16];
    char ampmBuf[8] = "";
    if (CLOCK_24H) {
        strftime(timeBuf, sizeof(timeBuf), "%H:%M", &timeinfo);
    } else {
        int hour12 = timeinfo.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;
        snprintf(timeBuf, sizeof(timeBuf), "%d:%02d", hour12, timeinfo.tm_min);
        strftime(ampmBuf, sizeof(ampmBuf), "%p", &timeinfo);
    }

    char dateBuf[24];
    strftime(dateBuf, sizeof(dateBuf), "%a %b %d", &timeinfo);
    for (char* p = dateBuf; *p; ++p) {
        *p = toupper(*p);
    }

    state.clockTimeStr = String(timeBuf);
    state.clockAmPmStr = String(ampmBuf);
    state.clockDateStr = String(dateBuf);

    bool minuteChanged = (timeinfo.tm_min != state.lastRenderedMinute);
    state.lastRenderedMinute = timeinfo.tm_min;
    return minuteChanged;
}

bool NetClient::httpRequest(const String& url, const char* method, const String& body, String& responseOut, int timeoutMs) {
    if (url.length() == 0 || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(timeoutMs);
    http.setConnectTimeout(timeoutMs);

    if (!http.begin(client, url)) {
        return false;
    }

    int code = -1;
    if (strcmp(method, "POST") == 0) {
        http.addHeader("Content-Type", "application/json");
        code = http.POST(body);
    } else {
        code = http.GET();
    }

    bool ok = (code >= 200 && code < 300);
    if (ok) {
        responseOut = http.getString();
    }
    http.end();
    return ok;
}

static void parseStatusJson(const String& payload, RadioProgramData& outRadio) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        return;
    }

    outRadio.state       = doc["state"] | "tuning";
    outRadio.freq        = doc["freq"] | outRadio.freq;
    outRadio.program     = doc["program"] | outRadio.program;
    outRadio.stationName = doc["station_name"] | "";
    outRadio.callsign    = doc["callsign"] | "";
    outRadio.slogan      = doc["slogan"] | "";
    outRadio.title       = doc["title"] | "";
    outRadio.artist      = doc["artist"] | "";
    outRadio.album       = doc["album"] | "";
    outRadio.genre       = doc["genre"] | "";
    outRadio.synced      = doc["synced"] | false;

    if (!doc["mer_db"].isNull()) {
        outRadio.merDb = doc["mer_db"].as<float>();
        outRadio.hasMer = true;
    } else {
        outRadio.merDb = 0.0f;
        outRadio.hasMer = false;
    }
    outRadio.nasOnline = true;
}

bool NetClient::fetchNasStatus(RadioProgramData& outRadio) {
    String url = String(NRSC5_API_BASE) + "/api/status";
    String resp;
    if (!httpRequest(url, "GET", "", resp, 1800)) {
        outRadio.nasOnline = false;
        return false;
    }
    parseStatusJson(resp, outRadio);
    return true;
}

bool NetClient::sendTune(float freq, int program, RadioProgramData& outRadio) {
    String url = String(NRSC5_API_BASE) + "/api/tune";
    char body[64];
    snprintf(body, sizeof(body), "{\"freq\":%.1f,\"program\":%d}", freq, program);

    String resp;
    if (!httpRequest(url, "POST", String(body), resp, 2000)) {
        outRadio.nasOnline = false;
        return false;
    }
    parseStatusJson(resp, outRadio);
    return true;
}

bool NetClient::triggerMediaPlayPause(AppState& state) {
    const char* method = MEDIA_HTTP_USE_POST ? "POST" : "GET";
    String resp;

    String playPauseUrl = String(MEDIA_PLAYPAUSE_URL);
    if (playPauseUrl.length() > 0) {
        bool ok = httpRequest(playPauseUrl, method, "", resp, 1800);
        if (ok) {
            state.mediaPlaying = !state.mediaPlaying;
        }
        syncMediaState(state);
        return ok;
    }

    bool targetPlay = !state.mediaPlaying;
    String targetUrl = targetPlay ? String(MEDIA_PLAY_URL) : String(MEDIA_PAUSE_URL);
    bool ok = httpRequest(targetUrl, method, "", resp, 1800);
    if (ok) {
        state.mediaPlaying = targetPlay;
    }
    syncMediaState(state);
    return ok;
}

bool NetClient::triggerMediaVolume(bool up) {
    String url = up ? String(MEDIA_VOLUME_UP_URL) : String(MEDIA_VOLUME_DOWN_URL);
    if (url.length() == 0) {
        return false;
    }
    const char* method = MEDIA_HTTP_USE_POST ? "POST" : "GET";
    String resp;
    return httpRequest(url, method, "", resp, 1500);
}

bool NetClient::syncMediaState(AppState& state) {
    String stateUrl = String(MEDIA_STATE_URL);
    if (stateUrl.length() == 0) {
        return false;
    }

    String resp;
    if (!httpRequest(stateUrl, "GET", "", resp, 1500)) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        return false;
    }

    const char* key = MEDIA_STATE_JSON_KEY;
    if (doc[key].is<const char*>()) {
        String val = doc[key].as<const char*>();
        state.mediaPlaying = val.equalsIgnoreCase(MEDIA_STATE_PLAYING_VALUE);
        return true;
    }
    return false;
}

bool NetClient::fetchWeather(WeatherData& outWeather) {
    const char* tempUnit = WEATHER_UNITS_F ? "fahrenheit" : "celsius";
    char url[256];
    snprintf(
        url,
        sizeof(url),
        "http://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,weather_code"
        "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max"
        "&temperature_unit=%s&timezone=auto&forecast_days=1",
        WEATHER_LAT,
        WEATHER_LON,
        tempUnit
    );

    String resp;
    if (!httpRequest(String(url), "GET", "", resp, 2500)) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        return false;
    }

    JsonObject current = doc["current"];
    JsonObject daily   = doc["daily"];
    if (current.isNull()) {
        return false;
    }

    outWeather.currentTemp = current["temperature_2m"] | 0.0f;
    outWeather.weatherCode = current["weather_code"] | 0;
    if (!daily.isNull()) {
        outWeather.highTemp   = daily["temperature_2m_max"][0] | outWeather.currentTemp;
        outWeather.lowTemp    = daily["temperature_2m_min"][0] | outWeather.currentTemp;
        outWeather.precipProb = daily["precipitation_probability_max"][0] | 0;
    }
    outWeather.valid = true;
    return true;
}
