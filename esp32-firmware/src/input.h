#pragma once
#include <Arduino.h>

enum class InputEvent : uint8_t {
    NONE = 0,
    MENU_SHORT_PRESS,
    MENU_LONG_PRESS,
    EXIT_PRESS,
    ROTARY_UP,
    ROTARY_DOWN,
    ROTARY_CLICK
};

class InputManager {
public:
    void begin();
    InputEvent poll();

private:
    struct ButtonTracker {
        uint8_t pin;
        bool lastRawState = HIGH;
        bool stableState = HIGH;
        bool longPressFired = false;
        unsigned long lastDebounceMs = 0;
        unsigned long pressedAtMs = 0;
    };

    ButtonTracker _btnMenu;
    ButtonTracker _btnExit;
    ButtonTracker _rotUp;
    ButtonTracker _rotDown;
    ButtonTracker _rotClick;

    static constexpr unsigned long DEBOUNCE_MS = 35;
    static constexpr unsigned long LONG_PRESS_MS = 1500;
};
