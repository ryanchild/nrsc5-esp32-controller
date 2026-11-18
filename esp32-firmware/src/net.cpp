#include "net.h"
#include "config.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

static const char* NWS_USER_AGENT = "(nrsc5-esp32-controller, github.com/ryanchild/nrsc5-esp32-controller)";

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

static void readStreamUpTo(WiFiClient* stream, String& responseOut, size_t maxBytes, int timeoutMs) {
    responseOut = "";
    responseOut.reserve(maxBytes + 1);
    unsigned long startMs = millis();
    uint8_t buf[512];
    while (stream && responseOut.length() < maxBytes && (millis() - startMs) < (unsigned long)timeoutMs) {
        size_t avail = stream->available();
        if (avail > 0) {
            size_t toRead = maxBytes - responseOut.length();
            if (toRead > sizeof(buf) - 1) toRead = sizeof(buf) - 1;
            if (toRead > avail) toRead = avail;
            int n = stream->read(buf, toRead);
            if (n > 0) {
                buf[n] = '\0';
                responseOut += (const char*)buf;
                startMs = millis();
            }
        } else if (!stream->connected()) {
            break;
        } else {
            delay(5);
        }
    }
}

bool NetClient::httpRequest(const String& url, const char* method, const String& body, String& responseOut, int timeoutMs, size_t maxBytes) {
    if (url.length() == 0 || WiFi.status() != WL_CONNECTED) {
        return false;
    }

    HTTPClient http;
    http.setTimeout(timeoutMs);
    http.setConnectTimeout(timeoutMs);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent(NWS_USER_AGENT);
    if (maxBytes > 0) {
        // Use HTTP/1.0 when reading a stream prefix so the server never uses chunked transfer encoding
        http.useHTTP10(true);
    }

    bool isHttps = url.startsWith("https://");
    bool ok = false;

    if (isHttps) {
        WiFiClientSecure secClient;
        secClient.setInsecure();
        secClient.setTimeout((timeoutMs + 999) / 1000);
        if (!http.begin(secClient, url)) {
            return false;
        }
        http.addHeader("Accept", "application/geo+json");
        int code = -1;
        if (strcmp(method, "POST") == 0) {
            http.addHeader("Content-Type", "application/json");
            code = http.POST(body);
        } else {
            code = http.GET();
        }
        ok = (code >= 200 && code < 300);
        if (ok) {
            if (maxBytes > 0) {
                readStreamUpTo(http.getStreamPtr(), responseOut, maxBytes, timeoutMs);
            } else {
                responseOut = http.getString();
            }
        }
        http.end();
    } else {
        WiFiClient client;
        client.setTimeout((timeoutMs + 999) / 1000);
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
        ok = (code >= 200 && code < 300);
        if (ok) {
            if (maxBytes > 0) {
                readStreamUpTo(http.getStreamPtr(), responseOut, maxBytes, timeoutMs);
            } else {
                responseOut = http.getString();
            }
        }
        http.end();
    }

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
    outRadio.composer    = doc["composer"] | "";
    outRadio.conductor   = doc["conductor"] | "";
    outRadio.orchestra   = doc["orchestra"] | "";
    outRadio.soloists    = doc["soloists"] | "";
    outRadio.performer   = doc["performer"] | "";
    outRadio.show        = doc["show"] | "";
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

static bool extractArrayElementJson(const String& src, const char* arrayKey, int elementIndex, String& outObj) {
    String keyToken = String("\"") + arrayKey + "\"";
    int pos = src.indexOf(keyToken);
    if (pos < 0) return false;

    int bracket = src.indexOf('[', pos + keyToken.length());
    if (bracket < 0) return false;

    int depth = 0;
    bool inStr = false;
    bool escape = false;
    int objStart = -1;
    int curIdx = 0;
    int len = src.length();

    for (int i = bracket + 1; i < len; ++i) {
        char c = src[i];
        if (inStr) {
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inStr = false;
            }
            continue;
        }
        if (c == '"') {
            inStr = true;
        } else if (c == '{') {
            if (depth == 0) {
                objStart = i;
            }
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0 && objStart >= 0) {
                if (curIdx == elementIndex) {
                    outObj = src.substring(objStart, i + 1);
                    return true;
                }
                curIdx++;
                objStart = -1;
            }
        } else if (c == ']' && depth == 0) {
            break;
        }
    }
    return false;
}

static float convertTempToConfiguredUnit(float val, const char* unitStr) {
    bool isCelsius = false;
    if (unitStr != nullptr) {
        String u(unitStr);
        u.toLowerCase();
        if (u == "c" || u.indexOf("degc") >= 0) {
            isCelsius = true;
        }
    }
    if (WEATHER_UNITS_F) {
        return isCelsius ? (val * 9.0f / 5.0f + 32.0f) : val;
    } else {
        return isCelsius ? val : ((val - 32.0f) * 5.0f / 9.0f);
    }
}

static int nwsToWmoCode(const String& forecastText, const String& iconUrl) {
    String txt = forecastText;
    txt.toLowerCase();
    String ico = iconUrl;
    ico.toLowerCase();

    if (txt.indexOf("thunder") >= 0 || txt.indexOf("t-storm") >= 0 || ico.indexOf("tsra") >= 0) {
        return 95; // Thunderstorm
    }
    if (txt.indexOf("snow") >= 0 || txt.indexOf("blizzard") >= 0 || txt.indexOf("flurr") >= 0 ||
        txt.indexOf("sleet") >= 0 || txt.indexOf("ice") >= 0 || txt.indexOf("wintry") >= 0 ||
        ico.indexOf("snow") >= 0 || ico.indexOf("blizzard") >= 0 || ico.indexOf("sleet") >= 0 || ico.indexOf("fzra") >= 0) {
        return 71; // Snow / Ice
    }
    if (txt.indexOf("rain") >= 0 || txt.indexOf("shower") >= 0 || txt.indexOf("drizzle") >= 0 ||
        ico.indexOf("rain") >= 0 || ico.indexOf("shra") >= 0) {
        return 61; // Rain / Showers
    }
    if (txt.indexOf("fog") >= 0 || txt.indexOf("mist") >= 0 || txt.indexOf("haze") >= 0 || txt.indexOf("smoke") >= 0 ||
        ico.indexOf("fog") >= 0 || ico.indexOf("haze") >= 0 || ico.indexOf("smoke") >= 0) {
        return 45; // Fog / Haze
    }
    if (txt.indexOf("cloud") >= 0 || txt.indexOf("overcast") >= 0 ||
        ico.indexOf("/ovc") >= 0 || ico.indexOf("/bkn") >= 0 || ico.indexOf("/sct") >= 0) {
        return 2;  // Partly Cloudy / Overcast
    }
    return 0;      // Clear / Sunny
}

bool NetClient::ensureNwsEndpoints() {
    if (_nwsForecastUrl.length() > 0 && _nwsHourlyUrl.length() > 0) {
        return true;
    }

    char pointsUrl[128];
    snprintf(
        pointsUrl,
        sizeof(pointsUrl),
        "https://api.weather.gov/points/%.4f,%.4f",
        WEATHER_LAT,
        WEATHER_LON
    );

    String resp;
    if (!httpRequest(String(pointsUrl), "GET", "", resp, 8000, 8192)) {
        return false;
    }

    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        return false;
    }

    JsonObject props = doc["properties"];
    if (props.isNull()) {
        return false;
    }

    _nwsForecastUrl = props["forecast"] | "";
    _nwsHourlyUrl   = props["forecastHourly"] | "";
    _nwsStationsUrl = props["observationStations"] | "";

    return (_nwsForecastUrl.length() > 0 && _nwsHourlyUrl.length() > 0);
}

bool NetClient::fetchNwsDailyForecast(float& outHigh, float& outLow, int& outPrecip, float& outFallbackTemp, int& outFallbackCode) {
    if (_nwsForecastUrl.length() == 0) {
        return false;
    }

    String resp;
    if (!httpRequest(_nwsForecastUrl, "GET", "", resp, 8000, 6144)) {
        return false;
    }

    String p0Str, p1Str;
    if (!extractArrayElementJson(resp, "periods", 0, p0Str)) {
        return false;
    }

    JsonDocument doc0;
    if (deserializeJson(doc0, p0Str) || doc0["temperature"].isNull()) {
        return false;
    }

    float t0 = convertTempToConfiguredUnit(doc0["temperature"].as<float>(), doc0["temperatureUnit"] | "F");
    bool day0 = doc0["isDaytime"] | true;
    int precip0 = doc0["probabilityOfPrecipitation"]["value"] | 0;
    outFallbackTemp = t0;
    outFallbackCode = nwsToWmoCode(doc0["shortForecast"] | "", doc0["icon"] | "");

    if (extractArrayElementJson(resp, "periods", 1, p1Str)) {
        JsonDocument doc1;
        if (!deserializeJson(doc1, p1Str) && !doc1["temperature"].isNull()) {
            float t1 = convertTempToConfiguredUnit(doc1["temperature"].as<float>(), doc1["temperatureUnit"] | "F");
            int precip1 = doc1["probabilityOfPrecipitation"]["value"] | 0;
            if (day0) {
                outHigh = t0;
                outLow  = t1;
            } else {
                outLow  = t0;
                outHigh = t1;
            }
            outPrecip = (precip0 >= precip1) ? precip0 : precip1;
            return true;
        }
    }

    outHigh   = t0;
    outLow    = t0;
    outPrecip = precip0;
    return true;
}

bool NetClient::fetchNwsHourlyCurrent(float& outTemp, int& outCode) {
    if (_nwsHourlyUrl.length() == 0) {
        return false;
    }

    String resp;
    if (!httpRequest(_nwsHourlyUrl, "GET", "", resp, 8000, 4096)) {
        return false;
    }

    String p0Str;
    if (!extractArrayElementJson(resp, "periods", 0, p0Str)) {
        return false;
    }

    JsonDocument doc0;
    if (deserializeJson(doc0, p0Str) || doc0["temperature"].isNull()) {
        return false;
    }

    outTemp = convertTempToConfiguredUnit(doc0["temperature"].as<float>(), doc0["temperatureUnit"] | "F");
    outCode = nwsToWmoCode(doc0["shortForecast"] | "", doc0["icon"] | "");
    return true;
}

bool NetClient::fetchNwsStationObservation(float& outTemp, int& outCode, float& outHigh24, float& outLow24, bool& outHasHiLo24) {
    outHasHiLo24 = false;

    if (_nwsLatestObsUrl.length() == 0) {
        if (_nwsStationsUrl.length() == 0) {
            return false;
        }
        String stResp;
        if (!httpRequest(_nwsStationsUrl, "GET", "", stResp, 8000, 4096)) {
            return false;
        }
        String feat0Str;
        if (!extractArrayElementJson(stResp, "features", 0, feat0Str)) {
            return false;
        }
        JsonDocument stDoc;
        if (deserializeJson(stDoc, feat0Str)) {
            return false;
        }
        String stationUrl = stDoc["id"] | "";
        if (stationUrl.length() == 0) {
            String stationId = stDoc["properties"]["stationIdentifier"] | "";
            if (stationId.length() > 0) {
                stationUrl = "https://api.weather.gov/stations/" + stationId;
            }
        }
        if (stationUrl.length() == 0) {
            return false;
        }
        _nwsLatestObsUrl = stationUrl + "/observations/latest";
    }

    String obsResp;
    if (!httpRequest(_nwsLatestObsUrl, "GET", "", obsResp, 8000, 8192)) {
        return false;
    }

    JsonDocument obsDoc;
    if (deserializeJson(obsDoc, obsResp)) {
        return false;
    }

    JsonObject props = obsDoc["properties"];
    if (props.isNull() || props["temperature"]["value"].isNull()) {
        return false;
    }

    outTemp = convertTempToConfiguredUnit(
        props["temperature"]["value"].as<float>(),
        props["temperature"]["unitCode"] | "wmoUnit:degC"
    );
    outCode = nwsToWmoCode(props["textDescription"] | "", props["icon"] | "");

    if (!props["maxTemperatureLast24Hours"]["value"].isNull() &&
        !props["minTemperatureLast24Hours"]["value"].isNull()) {
        outHigh24 = convertTempToConfiguredUnit(
            props["maxTemperatureLast24Hours"]["value"].as<float>(),
            props["maxTemperatureLast24Hours"]["unitCode"] | "wmoUnit:degC"
        );
        outLow24 = convertTempToConfiguredUnit(
            props["minTemperatureLast24Hours"]["value"].as<float>(),
            props["minTemperatureLast24Hours"]["unitCode"] | "wmoUnit:degC"
        );
        outHasHiLo24 = true;
    }

    return true;
}

bool NetClient::fetchWeather(WeatherData& outWeather) {
    if (!ensureNwsEndpoints()) {
        outWeather.valid = false;
        outWeather.error = true;
        return false;
    }

    float highTemp = 0.0f;
    float lowTemp  = 0.0f;
    int precipProb = 0;
    float fallbackTemp = 0.0f;
    int fallbackCode   = 0;
    bool hasDaily = fetchNwsDailyForecast(highTemp, lowTemp, precipProb, fallbackTemp, fallbackCode);

    // Prefer Option A: NWS 2.5km Grid Hourly Forecast
    float currentTemp = 0.0f;
    int weatherCode   = 0;
    bool hasCurrent   = fetchNwsHourlyCurrent(currentTemp, weatherCode);

    // Fallback to Option B: NWS Physical Observation Station (e.g. KFNL)
    if (!hasCurrent) {
        float high24 = 0.0f;
        float low24  = 0.0f;
        bool hasHiLo24 = false;
        if (fetchNwsStationObservation(currentTemp, weatherCode, high24, low24, hasHiLo24)) {
            hasCurrent = true;
            if (!hasDaily && hasHiLo24) {
                highTemp = high24;
                lowTemp  = low24;
                hasDaily = true;
            }
        }
    }

    // If both Option A and Option B failed, never display stale weather
    if (!hasCurrent) {
        outWeather.valid = false;
        outWeather.error = true;
        return false;
    }

    outWeather.currentTemp = currentTemp;
    outWeather.weatherCode = weatherCode;
    if (hasDaily) {
        outWeather.highTemp   = highTemp;
        outWeather.lowTemp    = lowTemp;
        outWeather.precipProb = precipProb;
    } else {
        outWeather.highTemp   = currentTemp;
        outWeather.lowTemp    = currentTemp;
        outWeather.precipProb = 0;
    }
    outWeather.valid = true;
    outWeather.error = false;
    return true;
}
