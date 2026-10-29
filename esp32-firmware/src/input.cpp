#include "input.h"
#include "config.h"

void InputManager::begin() {
    _btnMenu  = { PIN_BTN_MENU,     HIGH, HIGH, false, 0, 0 };
    _btnExit  = { PIN_BTN_EXIT,     HIGH, HIGH, false, 0, 0 };
    _rotUp    = { PIN_ROTARY_UP,    HIGH, HIGH, false, 0, 0 };
    _rotDown  = { PIN_ROTARY_DOWN,  HIGH, HIGH, false, 0, 0 };
    _rotClick = { PIN_ROTARY_CLICK, HIGH, HIGH, false, 0, 0 };

    pinMode(PIN_BTN_MENU,     INPUT_PULLUP);
    pinMode(PIN_BTN_EXIT,     INPUT_PULLUP);
    pinMode(PIN_ROTARY_UP,    INPUT_PULLUP);
    pinMode(PIN_ROTARY_DOWN,  INPUT_PULLUP);
    pinMode(PIN_ROTARY_CLICK, INPUT_PULLUP);
}

InputEvent InputManager::poll() {
    unsigned long now = millis();

    // 1. MENU button (supports short press and 1.5s long press)
    bool rawMenu = digitalRead(_btnMenu.pin);
    if (rawMenu != _btnMenu.lastRawState) {
        _btnMenu.lastDebounceMs = now;
        _btnMenu.lastRawState = rawMenu;
    }
    if ((now - _btnMenu.lastDebounceMs) >= DEBOUNCE_MS) {
        if (rawMenu != _btnMenu.stableState) {
            _btnMenu.stableState = rawMenu;
            if (_btnMenu.stableState == LOW) {
                _btnMenu.pressedAtMs = now;
                _btnMenu.longPressFired = false;
            } else {
                if (!_btnMenu.longPressFired) {
                    return InputEvent::MENU_SHORT_PRESS;
                }
            }
        } else if (_btnMenu.stableState == LOW && !_btnMenu.longPressFired) {
            if ((now - _btnMenu.pressedAtMs) >= LONG_PRESS_MS) {
                _btnMenu.longPressFired = true;
                return InputEvent::MENU_LONG_PRESS;
            }
        }
    }

    // Helper lambda for simple active-LOW press-on-down buttons
    auto checkSimplePress = [&](ButtonTracker& b) -> bool {
        bool raw = digitalRead(b.pin);
        if (raw != b.lastRawState) {
            b.lastDebounceMs = now;
            b.lastRawState = raw;
        }
        if ((now - b.lastDebounceMs) >= DEBOUNCE_MS && raw != b.stableState) {
            b.stableState = raw;
            if (b.stableState == LOW) {
                return true;
            }
        }
        return false;
    };

    if (checkSimplePress(_btnExit))  return InputEvent::EXIT_PRESS;
    if (checkSimplePress(_rotUp))    return InputEvent::ROTARY_UP;
    if (checkSimplePress(_rotDown))  return InputEvent::ROTARY_DOWN;
    if (checkSimplePress(_rotClick)) return InputEvent::ROTARY_CLICK;

    return InputEvent::NONE;
}
