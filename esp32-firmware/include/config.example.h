#pragma once
#include <Arduino.h>

// ============================================================================
// 1. Wi-Fi Credentials
// ============================================================================
#define WIFI_SSID     "YourWiFiSSID"
#define WIFI_PASSWORD "YourWiFiPassword"

// ============================================================================
// 2. Synology NAS nrsc5 Controller API
// ============================================================================
// Base URL for the nrsc5 FastAPI service running on your NAS (no trailing slash)
#define NRSC5_API_BASE "http://192.168.1.50:8751"

// ============================================================================
// 3. Generic Media Player Webhook URLs (node-sonos-http-api, Home Assistant, etc.)
// ============================================================================
// The controller triggers these HTTP URLs when you press EXIT (Play/Pause)
// or adjust the rotary dial in Volume mode. Leave any optional URL empty ("")
// if not needed.
#define MEDIA_PLAY_URL        "http://192.168.1.50:5005/Living%20Room/play"
#define MEDIA_PAUSE_URL       "http://192.168.1.50:5005/Living%20Room/pause"
#define MEDIA_PLAYPAUSE_URL   ""
#define MEDIA_VOLUME_UP_URL   "http://192.168.1.50:5005/Living%20Room/volume/+2"
#define MEDIA_VOLUME_DOWN_URL "http://192.168.1.50:5005/Living%20Room/volume/-2"

// Optional state endpoint to sync Play/Pause status (e.g. node-sonos-http-api /state)
// Leave MEDIA_STATE_URL empty ("") to track Play/Pause state locally.
#define MEDIA_STATE_URL           "http://192.168.1.50:5005/Living%20Room/state"
#define MEDIA_STATE_JSON_KEY      "playbackState"
#define MEDIA_STATE_PLAYING_VALUE "PLAYING"

// Use HTTP GET by default (set to true if your webhook requires HTTP POST)
#define MEDIA_HTTP_USE_POST   false

// ============================================================================
// 4. HD Radio Station Presets
// ============================================================================
// program: 0 = HD1, 1 = HD2, 2 = HD3, 3 = HD4
struct StationPreset {
    const char* name;
    float freq;
    int program;
};

static const StationPreset STATION_PRESETS[] = {
    { "KEXP 90.3 HD1",  90.3f,  0 },
    { "KUOW 94.9 HD1",  94.9f,  0 },
    { "KUOW 94.9 HD2",  94.9f,  1 },
    { "KING 98.1 HD1",  98.1f,  0 },
    { "KNDD 107.7 HD1", 107.7f, 0 },
};

static const size_t STATION_PRESET_COUNT =
    sizeof(STATION_PRESETS) / sizeof(STATION_PRESETS[0]);

// ============================================================================
// 5. Weather Configuration (Open-Meteo API - No API Key Required)
// ============================================================================
#define WEATHER_LAT      47.6062f
#define WEATHER_LON     -122.3321f
#define WEATHER_LABEL    "Seattle"
#define WEATHER_UNITS_F  true

// ============================================================================
// 6. Clock & Timezone Configuration
// ============================================================================
#define NTP_SERVER "pool.ntp.org"
// POSIX timezone string (handles Daylight Saving Time automatically)
// Examples:
//   Pacific:  "PST8PDT,M3.2.0,M11.1.0"
//   Mountain: "MST7MDT,M3.2.0,M11.1.0"
//   Central:  "CST6CDT,M3.2.0,M11.1.0"
//   Eastern:  "EST5EDT,M3.2.0,M11.1.0"
#define TZ_INFO    "PST8PDT,M3.2.0,M11.1.0"
#define CLOCK_24H  false

// ============================================================================
// 7. Controls & Timing Settings
// ============================================================================
// Default rotary switch mode at boot (true = Volume +/-, false = Station Prev/Next)
// Pressing the rotary switch (CLICK) toggles this mode at runtime.
#define ROTARY_DEFAULT_VOLUME_MODE true

// Polling intervals
#define STATUS_POLL_INTERVAL_MS    5000UL      // Poll NAS /api/status every 5s
#define WEATHER_POLL_INTERVAL_MS   900000UL    // Refresh Open-Meteo every 15m
#define TUNE_DEBOUNCE_MS           400UL       // Debounce rapid preset presses

// ============================================================================
// 8. Elecrow CrowPanel 5.79" Hardware Pin Definitions
// ============================================================================
#define EPD_PWR_PIN   7
#define EPD_SCK_PIN   12
#define EPD_MOSI_PIN  11
#define EPD_CS_PIN    45
#define EPD_DC_PIN    46
#define EPD_RST_PIN   47
#define EPD_BUSY_PIN  48

#define PIN_BTN_MENU      2
#define PIN_BTN_EXIT      1
#define PIN_ROTARY_UP     6
#define PIN_ROTARY_DOWN   4
#define PIN_ROTARY_CLICK  5
