#include "lowpower.h"
#include <Arduino.h>

// See lowpower.h: STOP-mode sleep disabled for now, MCU stays on and polls.

void lowPowerInit() {
}

void lowPowerAttachWakeButton(uint32_t pin, WakeCallback callback) {
    pinMode(pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(pin), callback, FALLING);
}

void lowPowerSleep() {
}
