#include "lowpower.h"
#include <Arduino.h>

void lowPowerInitButton(unsigned int pin) {
    pinMode(pin, INPUT_PULLUP);
}

void lowPowerSleep() {
    __WFI();
}
