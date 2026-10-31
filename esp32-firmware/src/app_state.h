#pragma once
#include <Arduino.h>

struct RadioProgramData {
    String state = "tuning";       // "tuning" | "playing" | "no_signal" | "error"
    float freq = 90.3f;
    int program = 0;               // 0 = HD1, 1 = HD2, etc.
    String presetName = "";
    String stationName = "";
    String callsign = "";
    String slogan = "";
    String title = "";
    String artist = "";
    String album = "";
    String genre = "";
    String composer = "";
    String conductor = "";
    String orchestra = "";
    String soloists = "";
    String performer = "";
    String show = "";
    float merDb = 0.0f;
    bool hasMer = false;
    bool synced = false;
    bool nasOnline = false;

    bool hasProgramMetadataChanged(const RadioProgramData& other) const {
        return state != other.state ||
               fabsf(freq - other.freq) > 0.05f ||
               program != other.program ||
               stationName != other.stationName ||
               callsign != other.callsign ||
               slogan != other.slogan ||
               title != other.title ||
               artist != other.artist ||
               album != other.album ||
               composer != other.composer ||
               conductor != other.conductor ||
               orchestra != other.orchestra ||
               soloists != other.soloists ||
               performer != other.performer ||
               show != other.show ||
               synced != other.synced ||
               nasOnline != other.nasOnline;
    }
};

struct WeatherData {
    bool valid = false;
    float currentTemp = 0.0f;
    float highTemp = 0.0f;
    float lowTemp = 0.0f;
    int precipProb = 0;
    int weatherCode = 0;
};

struct AppState {
    int presetIndex = 0;
    bool mediaPlaying = false;
    bool rotaryVolumeMode = true;
    bool wifiConnected = false;

    int lastRenderedMinute = -1;
    String clockTimeStr = "--:--";
    String clockAmPmStr = "";
    String clockDateStr = "--- --- --";

    RadioProgramData radio;
    WeatherData weather;
};
