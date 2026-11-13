#include "power.h"
#include "config.h"
#include <Arduino.h>
#include "stm32g0xx_ll_adc.h"
#include <IWatchdog.h>

void powerLatchOn() {
    pinMode(PIN_PWR_EN, OUTPUT);
    digitalWrite(PIN_PWR_EN, HIGH);
}

[[noreturn]] void powerOff() {
    ledOff();
    digitalWrite(PIN_PWR_EN, LOW);
    // From here the board is only kept alive by the power button itself (via
    // D1 -> SYSOFF). It dies the moment that is released. Park cheaply until
    // then.
    for (;;) {
        waitMs(10); // keep feeding: a held button must not trigger a watchdog reboot
    }
}

void ledOn() {
    pinMode(PIN_STATUS_LED, OUTPUT);
    digitalWrite(PIN_STATUS_LED, LOW);
}

void ledOff() {
    pinMode(PIN_STATUS_LED, INPUT); // anode is on VSYS: release, don't drive high
}

void ledBlink(uint8_t count, uint16_t onMs, uint16_t offMs) {
    for (uint8_t i = 0; i < count; i++) {
        ledOn();
        waitMs(onMs);
        ledOff();
        waitMs(offMs);
    }
}

bool powerButtonDown() {
    return digitalRead(PIN_RESET) == LOW;
}

uint16_t batteryReadMv() {
    analogReadResolution(12);

    // Average a handful of samples; the sense node has a 100nF cap on a 500k
    // source, so it is already smooth and this just trims ADC noise.
    const int N = 16;
    uint32_t sensSum = 0, vrefSum = 0;
    for (int i = 0; i < N; i++) {
        sensSum += analogRead(PIN_BATT_SENSE);
        vrefSum += analogRead(AVREF);
    }
    uint32_t sens = sensSum / N;
    uint32_t vref = vrefSum / N;
    if (vref == 0) return 0;

    // Actual VDDA from the factory VREFINT calibration.
    uint32_t vddaMv = __LL_ADC_CALC_VREFANALOG_VOLTAGE(vref, LL_ADC_RESOLUTION_12B);
    uint32_t pinMv = (sens * vddaMv) / 4095;
    return (uint16_t)(pinMv * BATT_DIVIDER_RATIO);
}

void wdtStart() {
    IWatchdog.begin((uint32_t)WDT_TIMEOUT_MS * 1000UL);
}

void wdtFeed() {
    IWatchdog.reload();
}

bool wdtCausedReset() {
    return IWatchdog.isReset(true);
}

void waitMs(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) {
        IWatchdog.reload();
        delay(1);
    }
}
