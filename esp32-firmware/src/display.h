#pragma once
#include <Arduino.h>
#include "app_state.h"

class DisplayManager {
public:
    void begin();

    // Full e-paper refresh (used on station cycle, 1-minute clock tick, and boot)
    void renderFull(const AppState& state);

    // Fast partial e-paper refresh (used every 5s when program metadata updates)
    void renderPartial(const AppState& state);

private:
    void drawDashboard(const AppState& state);
};
