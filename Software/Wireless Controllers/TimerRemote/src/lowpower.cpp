#include "lowpower.h"
#include <Arduino.h>
#include <STM32LowPower.h>

void lowPowerInit() {
    LowPower.begin();
}

void lowPowerAttachWakeButton(uint32_t pin, WakeCallback callback) {
    pinMode(pin, INPUT_PULLUP);
    LowPower.attachInterruptWakeup(pin, callback, FALLING, DEEP_SLEEP_MODE);
}

void lowPowerSleep() {
    LowPower.deepSleep();
}
