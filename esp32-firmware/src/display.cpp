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
static constexpr int16_t TOP_SPLIT_X = 425;
static constexpr int16_t TOP_ZONE_H  = 88;
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
    // 1. Horizontal divider separating Top Section (y: 0..87) from Full-Width Program Info (y: 88..245)
    epd.fillRect(0, TOP_ZONE_H, SCREEN_W, 2, GxEPD_BLACK);
    // 2. Vertical divider separating Upper-Left (Station/Frequency) from Upper-Right (Clock + Weather)
    epd.fillRect(TOP_SPLIT_X, 0, 2, TOP_ZONE_H, GxEPD_BLACK);

    // ========================================================================
    // ZONE 1: UPPER LEFT (x: 0..424, y: 0..87) — Frequency, Subchannel & Station
    // ========================================================================
    char freqStr[16];
    snprintf(freqStr, sizeof(freqStr), "%.1f", state.radio.freq);

    u8g2.setFont(u8g2_font_logisoso38_tn);
    u8g2.setForegroundColor(GxEPD_BLACK);
    int16_t freqX = 16;
    int16_t freqY = 46;
    u8g2.setCursor(freqX, freqY);
    u8g2.print(freqStr);
    int16_t freqW = u8g2.getUTF8Width(freqStr);

    // Inverted [HDx] Subchannel Badge next to frequency
    int16_t badgeX = freqX + freqW + 10;
    char hdLabel[12];
    snprintf(hdLabel, sizeof(hdLabel), "HD%d", state.radio.program + 1);

    u8g2.setFont(u8g2_font_helvB14_tr);
    int16_t hdTextW = u8g2.getUTF8Width(hdLabel);
    int16_t badgeW  = hdTextW + 14;
    int16_t badgeH  = 24;
    int16_t badgeY  = 20;

    epd.fillRoundRect(badgeX, badgeY, badgeW, badgeH, 4, GxEPD_BLACK);
    u8g2.setFontMode(1);
    u8g2.setForegroundColor(GxEPD_WHITE);
    u8g2.setBackgroundColor(GxEPD_BLACK);
    u8g2.setCursor(badgeX + 7, badgeY + 17);
    u8g2.print(hdLabel);
    u8g2.setForegroundColor(GxEPD_BLACK);
    u8g2.setBackgroundColor(GxEPD_WHITE);

    // Signal bars icon right beside the HD badge (MER text removed)
    int16_t barsX = badgeX + badgeW + 12;
    int16_t barsY = 24;
    Icons::drawSignalBars(epd, barsX, barsY, state.radio.merDb, state.radio.synced, GxEPD_BLACK);

    // Play / Pause pill badge in upper-left corner right side
    const char* playText = state.mediaPlaying ? "PLAYING" : "PAUSED";
    u8g2.setFont(u8g2_font_helvB10_tr);
    int16_t playW = u8g2.getUTF8Width(playText) + 24;
    int16_t playX = TOP_SPLIT_X - playW - 14;
    int16_t playY = 20;
    if (state.mediaPlaying) {
        epd.fillRoundRect(playX, playY, playW, 24, 4, GxEPD_BLACK);
        epd.fillTriangle(playX + 7, playY + 6, playX + 7, playY + 17, playX + 15, playY + 11, GxEPD_WHITE);
        u8g2.setFontMode(1);
        u8g2.setForegroundColor(GxEPD_WHITE);
        u8g2.setBackgroundColor(GxEPD_BLACK);
        u8g2.setCursor(playX + 18, playY + 16);
        u8g2.print(playText);
        u8g2.setForegroundColor(GxEPD_BLACK);
        u8g2.setBackgroundColor(GxEPD_WHITE);
    } else {
        epd.drawRoundRect(playX, playY, playW, 24, 4, GxEPD_BLACK);
        epd.fillRect(playX + 7, playY + 6, 3, 12, GxEPD_BLACK);
        epd.fillRect(playX + 12, playY + 6, 3, 12, GxEPD_BLACK);
        u8g2.setCursor(playX + 18, playY + 16);
        u8g2.print(playText);
    }

    // Station Line (Row 2): Clean station callsign / name without redundant "90.1 HD2"
    String stationName = state.radio.stationName.length() > 0 ? state.radio.stationName : state.radio.callsign;
    if (stationName.length() == 0) {
        stationName = state.radio.presetName;
        int sp = stationName.indexOf(' ');
        if (sp > 0 && isDigit(stationName.charAt(sp + 1))) {
            stationName = stationName.substring(0, sp);
        }
    }

    String stationLine = stationName;
    if (state.radio.show.length() > 0) {
        stationLine += "  ·  " + state.radio.show;
    } else if (state.radio.slogan.length() > 0 && !state.radio.slogan.startsWith("HD")) {
        stationLine += "  ·  " + state.radio.slogan;
    }

    u8g2.setFont(u8g2_font_helvB14_tr);
    u8g2.setCursor(16, 74);
    u8g2.print(fitTextToWidth(stationLine, TOP_SPLIT_X - 32));

    // ========================================================================
    // ZONE 2: UPPER RIGHT (x: 427..791, y: 0..87) — Clock + Weather
    // ========================================================================
    int16_t rightX = TOP_SPLIT_X + 16;
    int16_t rightMargin = SCREEN_W - 16;

    // Row 1: Large Clock (e.g. 14:20 or 2:20 PM)
    u8g2.setFont(u8g2_font_logisoso38_tn);
    u8g2.setCursor(rightX, 46);
    u8g2.print(state.clockTimeStr);
    int16_t clockW = u8g2.getUTF8Width(state.clockTimeStr.c_str());

    if (state.clockAmPmStr.length() > 0) {
        u8g2.setFont(u8g2_font_helvB12_tr);
        u8g2.setCursor(rightX + clockW + 6, 26);
        u8g2.print(state.clockAmPmStr);
    }

    // Date string pushed all the way to the upper right corner
    u8g2.setFont(u8g2_font_helvB14_tr);
    int16_t dateW = u8g2.getUTF8Width(state.clockDateStr.c_str());
    u8g2.setCursor(rightMargin - dateW, 26);
    u8g2.print(state.clockDateStr);

    // Row 2: Weather (Icon, Current Temp, High/Low, Rain Probability)
    if (state.weather.valid) {
        Icons::drawWeatherIcon(epd, rightX, 48, state.weather.weatherCode, GxEPD_BLACK);

        char tempNow[16];
        const char* unitChar = WEATHER_UNITS_F ? "F" : "C";
        snprintf(tempNow, sizeof(tempNow), "%.0f°%s", state.weather.currentTemp, unitChar);

        u8g2.setFont(u8g2_font_helvB18_tr);
        u8g2.setCursor(rightX + 42, 75);
        u8g2.print(tempNow);
        int16_t tempW = u8g2.getUTF8Width(tempNow);
        int16_t tempRight = rightX + 42 + tempW;

        // Precipitation right-aligned with the date above
        char rainBuf[24];
        snprintf(rainBuf, sizeof(rainBuf), "Rain %d%%", state.weather.precipProb);
        u8g2.setFont(u8g2_font_helvB18_tr);
        int16_t rainW = u8g2.getUTF8Width(rainBuf);
        int16_t rainX = rightMargin - rainW;
        u8g2.setCursor(rainX, 75);
        u8g2.print(rainBuf);

        // High / Low temp: same font/size as temperature (helvB18_tr), centered between temp and rain
        char hiLoBuf[32];
        snprintf(hiLoBuf, sizeof(hiLoBuf), "H:%.0f° L:%.0f°", state.weather.highTemp, state.weather.lowTemp);
        int16_t hiLoW = u8g2.getUTF8Width(hiLoBuf);
        int16_t availableSpace = rainX - tempRight;
        int16_t hiLoX;
        if (availableSpace > hiLoW) {
            hiLoX = tempRight + ((availableSpace - hiLoW) / 2);
        } else {
            hiLoX = tempRight + 8;
        }
        u8g2.setCursor(hiLoX, 75);
        u8g2.print(hiLoBuf);
    } else {
        u8g2.setFont(u8g2_font_helvR12_tr);
        u8g2.setCursor(rightX, 74);
        u8g2.print("Weather updating...");
    }

    // ========================================================================
    // ZONE 3: BOTTOM FULL WIDTH (x: 0..791, y: 88..245) — Now Playing
    // ========================================================================
    int16_t progMaxW = SCREEN_W - 32;
    String titleLine;
    String artistLine;
    String ensembleLine;
    String contextLine;

    bool isClassical = state.radio.composer.length() > 0;

    if (state.radio.title.length() > 0) {
        titleLine = state.radio.title;
        if (isClassical) {
            // Classical line 2: Composer and/or Soloists
            if (state.radio.soloists.length() > 0) {
                artistLine = state.radio.composer + "  ·  " + state.radio.soloists;
            } else {
                artistLine = state.radio.composer;
            }

            // Classical line 3: Orchestra and/or Conductor
            if (state.radio.orchestra.length() > 0 && state.radio.conductor.length() > 0) {
                ensembleLine = state.radio.orchestra + "  ·  " + state.radio.conductor + ", cond.";
            } else if (state.radio.orchestra.length() > 0) {
                ensembleLine = state.radio.orchestra;
            } else if (state.radio.conductor.length() > 0) {
                ensembleLine = state.radio.conductor + ", cond.";
            } else if (state.radio.performer.length() > 0) {
                ensembleLine = state.radio.performer;
            }

            // Classical line 4: Album or Show
            if (state.radio.album.length() > 0 && ensembleLine != state.radio.album) {
                contextLine = "Album: " + state.radio.album;
            } else if (state.radio.show.length() > 0) {
                contextLine = "Show: " + state.radio.show;
            }
        } else {
            // Non-classical (pop, rock, jazz, news)
            artistLine = state.radio.artist;
            if (state.radio.album.length() > 0) {
                ensembleLine = state.radio.album;
            }
            if (state.radio.genre.length() > 0 && !state.radio.genre.startsWith("HD")) {
                contextLine = state.radio.genre;
            }
        }
    } else if (state.radio.state == "tuning") {
        titleLine = "Tuning " + String(freqStr) + " FM " + String(hdLabel) + "...";
        artistLine = "Acquiring HD Radio Program Service Data";
    } else if (state.radio.slogan.length() > 0 && !state.radio.slogan.startsWith("HD")) {
        titleLine = state.radio.slogan;
        artistLine = stationLine;
    } else {
        titleLine = stationLine.length() > 0 ? stationLine : (String(freqStr) + " FM " + String(hdLabel));
        artistLine = state.radio.synced ? "Live HD Radio Broadcast" : "Waiting for station signal...";
    }

    // Draw Now Playing Label
    u8g2.setFont(u8g2_font_helvB10_tr);
    u8g2.setCursor(16, 106);
    u8g2.print("NOW PLAYING");

    int lineCount = 1;
    if (artistLine.length() > 0) lineCount++;
    if (ensembleLine.length() > 0) lineCount++;
    if (contextLine.length() > 0) lineCount++;

    if (lineCount >= 4) {
        // 4 lines (e.g. enriched classical with Title, Composer, Orchestra/Conductor, Album/Show)
        u8g2.setFont(u8g2_font_helvB18_tr);
        u8g2.setCursor(16, 134);
        u8g2.print(fitTextToWidth(titleLine, progMaxW));

        u8g2.setFont(u8g2_font_helvB14_tr);
        u8g2.setCursor(16, 168);
        u8g2.print(fitTextToWidth(artistLine, progMaxW));

        u8g2.setFont(u8g2_font_helvB14_tr);
        u8g2.setCursor(16, 198);
        u8g2.print(fitTextToWidth(ensembleLine, progMaxW));

        u8g2.setFont(u8g2_font_helvR12_tr);
        u8g2.setCursor(16, 226);
        u8g2.print(fitTextToWidth(contextLine, progMaxW));
    } else if (lineCount == 3) {
        // 3 lines (e.g. Title, Artist/Composer, Ensemble/Album)
        u8g2.setFont(u8g2_font_helvB18_tr);
        u8g2.setCursor(16, 140);
        u8g2.print(fitTextToWidth(titleLine, progMaxW));

        u8g2.setFont(u8g2_font_helvB14_tr);
        u8g2.setCursor(16, 178);
        u8g2.print(fitTextToWidth(artistLine, progMaxW));

        String line3 = ensembleLine.length() > 0 ? ensembleLine : contextLine;
        u8g2.setFont(u8g2_font_helvB14_tr);
        u8g2.setCursor(16, 212);
        u8g2.print(fitTextToWidth(line3, progMaxW));
    } else {
        // 1 or 2 lines (e.g. tuning or station without extra tags)
        u8g2.setFont(u8g2_font_helvB24_tr);
        u8g2.setCursor(16, 152);
        u8g2.print(fitTextToWidth(titleLine, progMaxW));

        if (artistLine.length() > 0) {
            u8g2.setFont(u8g2_font_helvB18_tr);
            u8g2.setCursor(16, 194);
            u8g2.print(fitTextToWidth(artistLine, progMaxW));
        }
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
    epd.powerOff();
}

void DisplayManager::renderPartial(const AppState& state) {
    epd.setPartialWindow(0, 0, SCREEN_W, SCREEN_H);
    epd.firstPage();
    do {
        drawDashboard(state);
    } while (epd.nextPage());
    epd.powerOff();
}
