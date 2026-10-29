#pragma once
#include <Adafruit_GFX.h>

namespace Icons {

// Draw weather condition icon (36x36 bounding box at x, y) using WMO weather_code
inline void drawWeatherIcon(Adafruit_GFX& gfx, int16_t x, int16_t y, int wmoCode, uint16_t color) {
    int16_t cx = x + 18;
    int16_t cy = y + 18;

    if (wmoCode == 0 || wmoCode == 1) {
        // Clear / Mostly Sunny: Sun with rays
        gfx.fillCircle(cx, cy, 8, color);
        for (int i = -12; i <= 12; i += 24) {
            gfx.drawLine(cx + i, cy, cx + (i > 0 ? 15 : -15), cy, color);
            gfx.drawLine(cx, cy + i, cx, cy + (i > 0 ? 15 : -15), color);
        }
        gfx.drawLine(cx - 9, cy - 9, cx - 12, cy - 12, color);
        gfx.drawLine(cx + 9, cy - 9, cx + 12, cy - 12, color);
        gfx.drawLine(cx - 9, cy + 9, cx - 12, cy + 12, color);
        gfx.drawLine(cx + 9, cy + 9, cx + 12, cy + 12, color);
    } else if (wmoCode >= 2 && wmoCode <= 48) {
        // Partly cloudy / Overcast / Fog: Cloud
        gfx.fillCircle(cx - 6, cy + 2, 7, color);
        gfx.fillCircle(cx + 2, cy - 2, 9, color);
        gfx.fillCircle(cx + 10, cy + 3, 6, color);
        gfx.fillRect(cx - 6, cy + 2, 16, 8, color);
    } else if ((wmoCode >= 51 && wmoCode <= 67) || (wmoCode >= 80 && wmoCode <= 82)) {
        // Drizzle / Rain / Showers: Cloud + rain drops
        gfx.fillCircle(cx - 6, cy - 2, 6, color);
        gfx.fillCircle(cx + 2, cy - 5, 8, color);
        gfx.fillCircle(cx + 9, cy - 1, 5, color);
        gfx.fillRect(cx - 6, cy - 2, 15, 7, color);
        for (int dx = -7; dx <= 7; dx += 7) {
            gfx.drawLine(cx + dx, cy + 8, cx + dx - 3, cy + 15, color);
            gfx.drawLine(cx + dx + 1, cy + 8, cx + dx - 2, cy + 15, color);
        }
    } else if ((wmoCode >= 71 && wmoCode <= 77) || (wmoCode >= 85 && wmoCode <= 86)) {
        // Snow: Cloud + snowflakes
        gfx.fillCircle(cx - 6, cy - 2, 6, color);
        gfx.fillCircle(cx + 2, cy - 5, 8, color);
        gfx.fillCircle(cx + 9, cy - 1, 5, color);
        gfx.fillRect(cx - 6, cy - 2, 15, 7, color);
        for (int dx = -6; dx <= 6; dx += 6) {
            int16_t sx = cx + dx;
            int16_t sy = cy + 12;
            gfx.drawLine(sx - 2, sy, sx + 2, sy, color);
            gfx.drawLine(sx, sy - 2, sx, sy + 2, color);
        }
    } else {
        // Thunderstorm: Cloud + lightning bolt
        gfx.fillCircle(cx - 6, cy - 3, 6, color);
        gfx.fillCircle(cx + 2, cy - 6, 8, color);
        gfx.fillCircle(cx + 9, cy - 2, 5, color);
        gfx.fillRect(cx - 6, cy - 3, 15, 7, color);
        gfx.drawLine(cx + 2, cy + 5, cx - 3, cy + 11, color);
        gfx.drawLine(cx - 3, cy + 11, cx + 2, cy + 11, color);
        gfx.drawLine(cx + 2, cy + 11, cx - 2, cy + 17, color);
    }
}

// Convert WMO weather code to short human-readable string
inline const char* weatherCodeToString(int code) {
    switch (code) {
        case 0: return "Clear";
        case 1: return "Mostly Clear";
        case 2: return "Partly Cloudy";
        case 3: return "Overcast";
        case 45:
        case 48: return "Foggy";
        case 51:
        case 53:
        case 55: return "Drizzle";
        case 56:
        case 57: return "Freezing Drzl";
        case 61:
        case 63:
        case 65: return "Rain";
        case 66:
        case 67: return "Freezing Rain";
        case 71:
        case 73:
        case 75:
        case 77: return "Snow";
        case 80:
        case 81:
        case 82: return "Rain Showers";
        case 85:
        case 86: return "Snow Showers";
        case 95:
        case 96:
        case 99: return "Thunderstorm";
        default: return "Fair";
    }
}

// Draw 4-bar signal indicator (width 22, height 16) based on MER dB
inline void drawSignalBars(Adafruit_GFX& gfx, int16_t x, int16_t y, float merDb, bool synced, uint16_t color) {
    int bars = 0;
    if (synced) {
        if (merDb >= 14.0f) bars = 4;
        else if (merDb >= 11.0f) bars = 3;
        else if (merDb >= 8.0f) bars = 2;
        else bars = 1;
    }
    for (int i = 0; i < 4; ++i) {
        int16_t bh = 4 + i * 4;
        int16_t bx = x + i * 6;
        int16_t by = y + (16 - bh);
        if (i < bars) {
            gfx.fillRect(bx, by, 4, bh, color);
        } else {
            gfx.drawRect(bx, by, 4, bh, color);
        }
    }
}

}  // namespace Icons
