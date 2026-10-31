#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "app_state.h"
#include "input.h"
#include "net.h"
#include "display.h"

static AppState       gState;
static InputManager   gInput;
static NetClient      gNet;
static DisplayManager gDisplay;

static unsigned long gLastStatusPollMs  = 0;
static unsigned long gLastWeatherPollMs = 0;
static unsigned long gLastClockCheckMs  = 0;

static bool          gPendingTune       = false;
static unsigned long gPendingTuneAtMs   = 0;

static void applyPresetToState(int index) {
    if (STATION_PRESET_COUNT == 0) return;
    if (index < 0) index = (int)STATION_PRESET_COUNT - 1;
    if (index >= (int)STATION_PRESET_COUNT) index = 0;

    gState.presetIndex       = index;
    gState.radio.freq        = STATION_PRESETS[index].freq;
    gState.radio.program     = STATION_PRESETS[index].program;
    gState.radio.presetName  = STATION_PRESETS[index].name;
    gState.radio.state       = "tuning";
    gState.radio.title       = "";
    gState.radio.artist      = "";
    gState.radio.album       = "";
    gState.radio.genre       = "";
    gState.radio.stationName = "";
    gState.radio.callsign    = "";
    gState.radio.slogan      = "";
    gState.radio.synced      = false;
}

static void syncPresetIndexFromRadio() {
    for (size_t i = 0; i < STATION_PRESET_COUNT; ++i) {
        if (fabsf(STATION_PRESETS[i].freq - gState.radio.freq) < 0.05f &&
            STATION_PRESETS[i].program == gState.radio.program) {
            gState.presetIndex = (int)i;
            gState.radio.presetName = STATION_PRESETS[i].name;
            return;
        }
    }
}

static void cycleStationPreset(int delta) {
    applyPresetToState(gState.presetIndex + delta);

    gNet.updateClock(gState);
    gDisplay.renderPartial(gState);

    gPendingTune     = true;
    gPendingTuneAtMs = millis();
}

void setup() {
    Serial.begin(115200);

    gState.rotaryVolumeMode = ROTARY_DEFAULT_VOLUME_MODE;
    applyPresetToState(0);

    gInput.begin();
    gDisplay.begin();
    gNet.begin();

    gState.wifiConnected = (WiFi.status() == WL_CONNECTED);
    gNet.updateClock(gState);

    if (gState.wifiConnected) {
        if (gNet.fetchNasStatus(gState.radio)) {
            syncPresetIndexFromRadio();
        }
        gNet.syncMediaState(gState);
        gNet.fetchWeather(gState.weather);
    }

    // Initial full refresh at boot
    gDisplay.renderFull(gState);

    unsigned long now = millis();
    gLastStatusPollMs  = now;
    gLastWeatherPollMs = now;
    gLastClockCheckMs  = now;
}

void loop() {
    unsigned long now = millis();

    // 1. Poll physical buttons & rotary switch
    InputEvent ev = gInput.poll();
    switch (ev) {
        case InputEvent::MENU_SHORT_PRESS:
            cycleStationPreset(+1);
            break;

        case InputEvent::MENU_LONG_PRESS:
            gNet.updateClock(gState);
            gDisplay.renderFull(gState);
            break;

        case InputEvent::EXIT_PRESS:
            gNet.triggerMediaPlayPause(gState);
            gDisplay.renderPartial(gState);
            break;

        case InputEvent::ROTARY_UP:
            if (gState.rotaryVolumeMode) {
                gNet.triggerMediaVolume(true);
            } else {
                cycleStationPreset(+1);
            }
            break;

        case InputEvent::ROTARY_DOWN:
            if (gState.rotaryVolumeMode) {
                gNet.triggerMediaVolume(false);
            } else {
                cycleStationPreset(-1);
            }
            break;

        case InputEvent::ROTARY_CLICK:
            gState.rotaryVolumeMode = !gState.rotaryVolumeMode;
            gDisplay.renderPartial(gState);
            break;

        case InputEvent::NONE:
        default:
            break;
    }

    // 2. Execute debounced tune command to NAS after station cycle
    if (gPendingTune && (now - gPendingTuneAtMs) >= TUNE_DEBOUNCE_MS) {
        gPendingTune = false;
        RadioProgramData tunedRadio = gState.radio;
        gNet.sendTune(gState.radio.freq, gState.radio.program, tunedRadio);
        tunedRadio.presetName = gState.radio.presetName;
        gState.radio = tunedRadio;
        gLastStatusPollMs = millis();
    }

    // 3. Check clock every second -> Full refresh at top of the hour, partial refresh on other minutes
    if ((now - gLastClockCheckMs) >= 1000UL) {
        gLastClockCheckMs = now;
        gState.wifiConnected = gNet.ensureWiFi();
        if (gNet.updateClock(gState)) {
            if (gState.lastRenderedMinute == 0) {
                gDisplay.renderFull(gState);
            } else {
                gDisplay.renderPartial(gState);
            }
        }
    }

    // 4. Poll NAS /api/status every 5 seconds -> Partial refresh when program data changes
    if (!gPendingTune && (now - gLastStatusPollMs) >= STATUS_POLL_INTERVAL_MS) {
        gLastStatusPollMs = now;
        if (gState.wifiConnected) {
            RadioProgramData latest = gState.radio;
            bool ok = gNet.fetchNasStatus(latest);
            if (ok) {
                syncPresetIndexFromRadio();
                latest.presetName = STATION_PRESETS[gState.presetIndex].name;
            }
            bool prevPlay = gState.mediaPlaying;
            gNet.syncMediaState(gState);

            if (latest.hasProgramMetadataChanged(gState.radio) || gState.mediaPlaying != prevPlay) {
                gState.radio = latest;
                gDisplay.renderPartial(gState);
            } else {
                gState.radio = latest;
            }
        }
    }

    // 5. Refresh Open-Meteo weather every 15 minutes -> Partial refresh
    if ((now - gLastWeatherPollMs) >= WEATHER_POLL_INTERVAL_MS) {
        gLastWeatherPollMs = now;
        if (gState.wifiConnected) {
            if (gNet.fetchWeather(gState.weather)) {
                gDisplay.renderPartial(gState);
            }
        }
    }

    delay(10);
}
