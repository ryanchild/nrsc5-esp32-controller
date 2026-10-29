#include "display.h"
#include "config.h"
#include "icons.h"
#include <SPI.h>
#include <GxEPD2_BW.h>
#include <U8g2_for_Adafruit_GFX.h>

// Elecrow CrowPanel 5.79" 792x272 dual SSD1683 active-matrix e-paper display
static GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> epd(
    GxEPD2_579_GDEY0579T93(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN)
);

static U8G2_FOR_ADAFRUIT_GFX u8g2;

static constexpr int16_t SCREEN_W = 792;
static constexpr int16_t SCREEN_H = 272;
static constexpr int16_t TOP_SPLIT_X = 435;
static constexpr int16_t TOP_ZONE_H  = 122;
static constexpr int16_t FOOTER_Y    = 246;

void DisplayManager::begin() {
    // Power on the CrowPanel 5.79" e-paper driver circuit
    pinMode(EPD_PWR_PIN, OUTPUT);
    digitalWrite(EPD_PWR_PIN, HIGH);
    delay(50);

    // Initialize explicit SPI pins for CrowPanel ESP32-S3
    SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, EPD_CS_PIN);

    epd.init(115200, true, 2, false);
    epd.setRotation(0);

    u8g2.begin(epd);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);
}

static String fitTextToWidth(const String& text, int16_t maxWidthPx) {
    if (text.length() == 0) return "";
    if (u8g2.getUTF8Width(text.c_str()) <= maxWidthPx) {
        return text;
    }
    String trimmed = text;
    while (trimmed.length() > 1 && u8g2.getUTF8Width((trimmed + "...").c_str()) > maxWidthPx) {
        trimmed.remove(trimmed.length() - 1);
    }
    return trimmed + "...";
}

void DisplayManager::drawDashboard(const AppState& state) {
    epd.fillScreen(GxEPD_WHITE);

    // Structural dividers:
    // 1. Horizontal divider separating Top Section (y: 0..121) from Full-Width Program Info (y: 122..245)
    epd.fillRect(0, TOP_ZONE_H, SCREEN_W, 2, GxEPD_BLACK);
    // 2. Vertical divider separating Upper-Left (Station/Frequency) from Upper-Right (Clock + Weather)
    epd.fillRect(TOP_SPLIT_X, 0, 2, TOP_ZONE_H, GxEPD_BLACK);

    // ========================================================================
    // ZONE 1: UPPER LEFT (x: 0..434, y: 0..121) — Frequency, Subchannel & Status
    // ========================================================================
    char freqStr[16];
    snprintf(freqStr, sizeof(freqStr), "%.1f", state.radio.freq);

    u8g2.setFont(u8g2_font_logisoso42_tn);
    u8g2.setForegroundColor(GxEPD_BLACK);
    int16_t freqX = 16;
    int16_t freqY = 56;
    u8g2.setCursor(freqX, freqY);
    u8g2.print(freqStr);
    int16_t freqW = u8g2.getUTF8Width(freqStr);

    // "FM" label + Inverted [HDx] Subchannel Badge next to frequency
    int16_t badgeX = freqX + freqW + 12;
    char hdLabel[12];
    snprintf(hdLabel, sizeof(hdLabel), "HD%d", state.radio.program + 1);

    u8g2.setFont(u8g2_font_helvB18_tr);
    int16_t hdTextW = u8g2.getUTF8Width(hdLabel);
    int16_t badgeW  = hdTextW + 18;
    int16_t badgeH  = 28;
    int16_t badgeY  = 16;

    epd.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 5, GxEPD_BLACK);
    u8g2.setForegroundColor(GxEPD_WHITE);
    u8g2.setCursor(badgeX + 9, badgeY + 21);
    u8g2.print(hdLabel);
    u8g2.setForegroundColor(GxEPD_BLACK);

    // Play / Pause pill badge in upper-left corner right side
    const char* playText = state.mediaPlaying ? "PLAYING" : "PAUSED";
    u8g2.setFont(u8g2_font_helvB10_tr);
    int16_t playW = u8g2.getUTF8Width(playText) + 26;
    int16_t playX = TOP_SPLIT_X - playW - 14;
    int16_t playY = 16;
    if (state.mediaPlaying) {
        epd.fillRoundRect(playX, playY, playW, 24, 4, GxEPD_BLACK);
        // Play triangle
        epd.fillTriangle(playX + 8, playY + 6, playX + 8, playY + 17, playX + 16, playY + 11, GxEPD_WHITE);
        u8g2.setForegroundColor(GxEPD_WHITE);
        u8g2.setCursor(playX + 20, playY + 16);
        u8g2.print(playText);
        u8g2.setForegroundColor(GxEPD_BLACK);
    } else {
        epd.drawRoundRect(playX, playY, playW, 24, 4, GxEPD_BLACK);
        epd.fillRect(playX + 8, playY + 6, 3, 12, GxEPD_BLACK);
        epd.fillRect(playX + 13, playY + 6, 3, 12, GxEPD_BLACK);
        u8g2.setCursor(playX + 20, playY + 16);
        u8g2.print(playText);
    }

    // Station Callsign & Preset Name on second row of Upper Left
    String stationLine = state.radio.stationName.length() > 0
                             ? state.radio.stationName
                             : (state.radio.callsign.length() > 0 ? state.radio.callsign : state.radio.presetName);
    if (state.radio.presetName.length() > 0 && stationLine != state.radio.presetName) {
        stationLine += "  ·  " + state.radio.presetName;
    }

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor(16, 86);
    u8g2.print(fitTextToWidth(stationLine, TOP_SPLIT_X - 32));

    // Signal MER bars & status on third row of Upper Left
    Icons::drawSignalBars(epd, 16, 96, state.radio.merDb, state.radio.synced, GxEPD_BLACK);
    u8g2.setFont(u8g2_font_helvR10_tr);
    u8g2.setCursor(46, 110);
    if (!state.wifiConnected) {
        u8g2.print("Wi-Fi Reconnecting...");
    } else if (!state.radio.nasOnline) {
        u8g2.print("NAS Offline");
    } else if (state.radio.state == "tuning") {
        u8g2.print("Acquiring HD Lock...");
    } else if (state.radio.synced && state.radio.hasMer) {
        char sigBuf[40];
        snprintf(sigBuf, sizeof(sigBuf), "HD Locked  ·  MER %.1f dB", state.radio.merDb);
        u8g2.print(sigBuf);
    } else if (state.radio.synced) {
        u8g2.print("HD Locked");
    } else {
        u8g2.print("No HD Lock");
    }

    // ========================================================================
    // ZONE 2: UPPER RIGHT (x: 437..791, y: 0..121) — Clock + Weather
    // ========================================================================
    int16_t rightX = TOP_SPLIT_X + 18;

    // Large Clock (e.g. 14:20 or 2:20 PM)
    u8g2.setFont(u8g2_font_logisoso38_tn);
    u8g2.setCursor(rightX, 52);
    u8g2.print(state.clockTimeStr);
    int16_t clockW = u8g2.getUTF8Width(state.clockTimeStr.c_str());

    if (state.clockAmPmStr.length() > 0) {
        u8g2.setFont(u8g2_font_helvB12_tr);
        u8g2.setCursor(rightX + clockW + 6, 30);
        u8g2.print(state.clockAmPmStr);
    }

    // Date string aligned right in Upper-Right header
    u8g2.setFont(u8g2_font_helvB12_tr);
    int16_t dateW = u8g2.getUTF8Width(state.clockDateStr.c_str());
    u8g2.setCursor(SCREEN_W - dateW - 16, 30);
    u8g2.print(state.clockDateStr);

    // City label under date
    u8g2.setFont(u8g2_font_helvR10_tr);
    int16_t cityW = u8g2.getUTF8Width(WEATHER_LABEL);
    u8g2.setCursor(SCREEN_W - cityW - 16, 48);
    u8g2.print(WEATHER_LABEL);

    // Weather Row (y: 64..116)
    epd.drawFastHLine(rightX, 62, SCREEN_W - rightX - 16, GxEPD_BLACK);
    if (state.weather.valid) {
        Icons::drawWeatherIcon(epd, rightX, 72, state.weather.weatherCode, GxEPD_BLACK);

        char tempNow[20];
        const char* unitChar = WEATHER_UNITS_F ? "F" : "C";
        snprintf(tempNow, sizeof(tempNow), "%.0f°%s", state.weather.currentTemp, unitChar);

        u8g2.setFont(u8g2_font_helvB18_tr);
        u8g2.setCursor(rightX + 44, 95);
        u8g2.print(tempNow);
        int16_t tempW = u8g2.getUTF8Width(tempNow);

        int16_t detailX = rightX + 44 + tempW + 14;
        u8g2.setFont(u8g2_font_helvB12_tr);
        u8g2.setCursor(detailX, 86);
        u8g2.print(Icons::weatherCodeToString(state.weather.weatherCode));

        char hiLoBuf[48];
        snprintf(
            hiLoBuf,
            sizeof(hiLoBuf),
            "H:%.0f°  L:%.0f°  ·  Rain %d%%",
            state.weather.highTemp,
            state.weather.lowTemp,
            state.weather.precipProb
        );
        u8g2.setFont(u8g2_font_helvR10_tr);
        u8g2.setCursor(detailX, 106);
        u8g2.print(hiLoBuf);
    } else {
        u8g2.setFont(u8g2_font_helvR12_tr);
        u8g2.setCursor(rightX, 94);
        u8g2.print("Weather updating...");
    }

    // ========================================================================
    // ZONE 3: BOTTOM FULL WIDTH (x: 0..791, y: 124..245) — Program Info
    // ========================================================================
    int16_t progMaxW = SCREEN_W - 32;
    String primaryLine;
    String secondaryLine;
    String tertiaryLine;

    if (state.radio.title.length() > 0) {
        primaryLine = state.radio.title;
        if (state.radio.artist.length() > 0 && state.radio.album.length() > 0) {
            secondaryLine = state.radio.artist + "  —  " + state.radio.album;
        } else if (state.radio.artist.length() > 0) {
            secondaryLine = state.radio.artist;
        } else if (state.radio.album.length() > 0) {
            secondaryLine = state.radio.album;
        }
        if (state.radio.slogan.length() > 0) {
            tertiaryLine = state.radio.slogan;
        } else if (state.radio.genre.length() > 0) {
            tertiaryLine = state.radio.genre;
        }
    } else if (state.radio.state == "tuning") {
        primaryLine = "Tuning " + String(freqStr) + " FM " + String(hdLabel) + "...";
        secondaryLine = "Waiting for HD Radio Program Service Data";
    } else if (state.radio.slogan.length() > 0) {
        primaryLine = state.radio.slogan;
        secondaryLine = state.radio.stationName.length() > 0 ? state.radio.stationName : state.radio.presetName;
    } else {
        primaryLine = state.radio.presetName.length() > 0 ? state.radio.presetName : (String(freqStr) + " FM " + String(hdLabel));
        secondaryLine = state.radio.synced ? "Live HD Radio Broadcast" : "Waiting for station signal...";
    }

    // Draw Now Playing Label
    u8g2.setFont(u8g2_font_helvB10_tr);
    u8g2.setCursor(16, 144);
    u8g2.print("NOW PLAYING");

    // Draw Large Full-Width Song Title
    u8g2.setFont(u8g2_font_helvB24_tr);
    u8g2.setCursor(16, 182);
    u8g2.print(fitTextToWidth(primaryLine, progMaxW));

    // Draw Full-Width Artist / Album line
    if (secondaryLine.length() > 0) {
        u8g2.setFont(u8g2_font_helvB14_tr);
        u8g2.setCursor(16, 212);
        u8g2.print(fitTextToWidth(secondaryLine, progMaxW));
    }

    // Draw optional Slogan / Genre line
    if (tertiaryLine.length() > 0) {
        u8g2.setFont(u8g2_font_helvR12_tr);
        u8g2.setCursor(16, 234);
        u8g2.print(fitTextToWidth(tertiaryLine, progMaxW));
    }

    // ========================================================================
    // FOOTER BAR (x: 0..791, y: 246..271) — Controls Legend
    // ========================================================================
    epd.fillRect(0, FOOTER_Y, SCREEN_W, SCREEN_H - FOOTER_Y, GxEPD_BLACK);
    u8g2.setFont(u8g2_font_helvB10_tr);
    u8g2.setForegroundColor(GxEPD_WHITE);
    u8g2.setBackgroundColor(GxEPD_BLACK);

    u8g2.setCursor(16, FOOTER_Y + 17);
    u8g2.print("[MENU] Next Station (Hold: Refresh)");

    u8g2.setCursor(310, FOOTER_Y + 17);
    u8g2.print("[EXIT] Play / Pause");

    const char* dialLabel = state.rotaryVolumeMode
                                ? "[DIAL] Volume +/- (Click: Tune Mode)"
                                : "[DIAL] Prev/Next Station (Click: Vol Mode)";
    int16_t dialW = u8g2.getUTF8Width(dialLabel);
    u8g2.setCursor(SCREEN_W - dialW - 16, FOOTER_Y + 17);
    u8g2.print(dialLabel);

    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);
}

void DisplayManager::renderFull(const AppState& state) {
    epd.setFullWindow();
    epd.firstPage();
    do {
        drawDashboard(state);
    } while (epd.nextPage());
    epd.hibernate();
}

void DisplayManager::renderPartial(const AppState& state) {
    epd.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
    epd.firstPage();
    do {
        drawDashboard(state);
    } while (epd.nextPage());
    epd.hibernate();
}
