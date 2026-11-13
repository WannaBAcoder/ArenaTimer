#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "packet.h"
#include "pairing.h"
#include "radio.h"
#include "lowpower.h"
#include "power.h"
#include "roleConfig.h"

// One firmware image for every board. Power flow:
//
//   power button pressed -> board powers up (hardware) -> latch it on
//   -> read role jumpers -> make sure the radio runs the P2P firmware
//   (flash it if not) -> check the battery -> ready (one blink)
//   -> sleep between presses, wake and send immediately.
//
// Status LED patterns (blue D11):
//   solid during boot (stays solid while the radio is being flashed - don't
//     pull the battery)
//   1 blink            ready
//   3 slow blinks      battery too low to run, powering off
//   short flash ~1Hz   battery low (below BATT_LOW_WARN_MV), still running
//   6 fast blinks      radio failed to start / flash, powering off
//   during a long hold: 3 blinks, 1s apart, then solid = release to power off
//
// Packets: button presses are the original 6-byte RemotePacket, unchanged.
// A separate 7-byte RemoteStatusPacket carries the battery level, sent once
// at boot and every ~minute (see packet.h for why it is separate).

// ---- timing -----------------------------------------------------------------
#define DEBOUNCE_MS 20
// Hold the power button at least this long before the power-off countdown
// begins; shorter presses are ordinary button presses.
#define POWEROFF_ARM_MS 1000
#define POWEROFF_BLINKS 3
#define STATUS_INTERVAL_MS 60000UL
// Minimum spacing between two consecutive transmissions: lets one packet
// leave the antenna before the next AT command arrives.
#define MIN_TX_GAP_MS 120

// Buzzer: the ESP32 timer (timerLogic.cpp triggerBeep()) extends its
// beep-off timer by 250ms on every BTN_BUZZER packet and lets the tone lapse
// if packets stop, so re-sending well inside that window keeps the tone
// continuous at a small fraction of the old ESP-NOW rate.
#define BUZZER_REPEAT_MS 100

// Power off after this long without a single button press (status frames
// don't count). Covers a lunch break; forgotten remotes don't drain the pack.
#ifndef IDLE_POWEROFF_MS // overridable for bench testing: -D IDLE_POWEROFF_MS=30000UL
#define IDLE_POWEROFF_MS (2UL * 60UL * 60UL * 1000UL)
#endif

// How long the status LED flashes when a button message goes out. A message
// the radio refused instead gets two extra quick flashes.
#define TX_FLASH_MS 40

static uint8_t deviceId[4];
static bool isJudge = false;
static uint8_t myRole = ROLE_JUDGE;
static uint32_t lastTxMs = 0;
static uint32_t nextStatusMs = 0;
static uint32_t lastActivityMs = 0;
static uint32_t nextBattCheckMs = 0;
static bool lowBattery = false;
static uint8_t shutdownReadings = 0;

// STM32duino calls this before setup(). The power button only keeps the
// board alive while it is physically held, so latching must come first.
void initVariant() {
    powerLatchOn();
}

// ---- role jumpers ------------------------------------------------------------
// Sampled once with the internal pull-ups, then returned to analog mode so
// the (possibly floating) pins draw nothing while asleep.
static void readRoleJumpers() {
    pinMode(PIN_ROLE_JUDGE, INPUT_PULLUP);
    pinMode(PIN_ROLE_COLOR, INPUT_PULLUP);
    waitMs(2);
    bool ra1Fitted = (digitalRead(PIN_ROLE_JUDGE) == LOW);
    bool rb1Fitted = (digitalRead(PIN_ROLE_COLOR) == LOW);
    pinMode(PIN_ROLE_JUDGE, INPUT_ANALOG);
    pinMode(PIN_ROLE_COLOR, INPUT_ANALOG);

    isJudge = JUDGE_WHEN_RA1_FITTED ? ra1Fitted : !ra1Fitted;
    bool isBlue = BLUE_WHEN_RB1_FITTED ? rb1Fitted : !rb1Fitted;
    myRole = isJudge ? ROLE_JUDGE : (isBlue ? ROLE_BLUE_READY : ROLE_RED_READY);
}

// ---- transmit ----------------------------------------------------------------
static void transmit(const uint8_t* data, size_t len, bool flashLed) {
    while (millis() - lastTxMs < MIN_TX_GAP_MS) {
        waitMs(1);
    }
    uint32_t start = millis();
    if (flashLed) ledOn();
    bool accepted = radioSendPacket(data, len);
    lastTxMs = millis();
    if (flashLed) {
        // The flash starts as the command is handed to the radio, so it adds
        // no latency to the message itself.
        while (millis() - start < TX_FLASH_MS) waitMs(1);
        ledOff();
        if (!accepted) {
            waitMs(80);
            ledBlink(2, 60, 80);
        }
    }
}

static void sendButton(uint8_t buttonId) {
    RemotePacket pkt;
    memcpy(pkt.deviceId, deviceId, sizeof(deviceId));
    pkt.role = myRole;
    pkt.buttonId = buttonId;
    transmit(reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt), true);
}

static void sendStatus(uint16_t batteryMv) {
    RemoteStatusPacket pkt;
    memcpy(pkt.deviceId, deviceId, sizeof(deviceId));
    pkt.role = myRole;
    pkt.buttonId = BTN_STATUS;
    pkt.battery = batteryMvToCode(batteryMv);
    transmit(reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt), false);
}

// ---- power-off gesture -------------------------------------------------------
// True if the power button stayed down for the whole interval.
static bool holdFor(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) {
        if (!powerButtonDown()) return false;
        waitMs(2);
    }
    return true;
}

// Called once the button has been held POWEROFF_ARM_MS. Counts down with LED
// blinks, then waits for release. Returns only if the hold was abandoned
// early (no command is sent in that case).
static void powerOffGesture() {
    for (uint8_t n = 1; n <= POWEROFF_BLINKS; n++) {
        ledOn();
        if (!holdFor(200)) {
            if (n == POWEROFF_BLINKS) powerOff(); // released during the last blink: committed
            ledOff();
            return;
        }
        if (n < POWEROFF_BLINKS) {
            ledOff();
            if (!holdFor(800)) return;
        }
    }
    // LED stays solid: release now to power off.
    while (powerButtonDown()) waitMs(5);
    powerOff();
}

// ---- buttons -----------------------------------------------------------------
// The power button (PA6) is also the Judge's RESET and the ready remote's
// only button. Its command is sent on RELEASE, and only if it was a short
// tap - otherwise holding it to power off would first fire a RESET / ready /
// tap-out at the timer. Cost: the command goes out when the finger lifts
// (typically 50-150ms after the press) instead of the instant it lands.
static void handlePowerButton(uint8_t buttonId) {
    waitMs(DEBOUNCE_MS);
    if (!powerButtonDown()) return; // noise
    lastActivityMs = millis();

    uint32_t start = millis();
    while (powerButtonDown()) {
        if (millis() - start >= POWEROFF_ARM_MS) {
            powerOffGesture();
            while (powerButtonDown()) waitMs(5);
            waitMs(DEBOUNCE_MS);
            return;
        }
        waitMs(2);
    }
    waitMs(DEBOUNCE_MS);
    sendButton(buttonId);
}

// Everything else is immediate: debounce, send once, wait for release.
// BUZZER repeats while held (see BUZZER_REPEAT_MS).
static void handleButton(uint32_t pin, uint8_t buttonId) {
    waitMs(DEBOUNCE_MS);
    if (digitalRead(pin) != LOW) return; // noise
    lastActivityMs = millis();

    sendButton(buttonId);

    if (buttonId == BTN_BUZZER) {
        uint32_t lastSend = millis();
        while (digitalRead(pin) == LOW) {
            if (millis() - lastSend >= BUZZER_REPEAT_MS) {
                lastSend = millis();
                sendButton(buttonId);
            }
            waitMs(5);
        }
    } else {
        while (digitalRead(pin) == LOW) waitMs(5);
    }
    waitMs(DEBOUNCE_MS);
}

// ---- battery / status ---------------------------------------------------------
static void powerOffLowBattery() {
    ledBlink(3, 400, 300);
    powerOff();
}

// Measures the battery every BATT_CHECK_MS: sets the low-battery flag (LED
// flashing, with hysteresis) and powers the remote off, after telling the
// timer, once the voltage has stayed under BATT_SHUTDOWN_MV.
static void batteryMonitor() {
    if ((int32_t)(millis() - nextBattCheckMs) < 0) return;
    // A transmission pulls up to ~120mA; don't measure while the cell is
    // still sagging from one.
    if (millis() - lastTxMs < 500) return;
    nextBattCheckMs = millis() + BATT_CHECK_MS;

    uint16_t mv = batteryReadMv();

    if (!lowBattery && mv < BATT_LOW_WARN_MV) lowBattery = true;
    else if (lowBattery && mv > BATT_LOW_CLEAR_MV) lowBattery = false;

    if (mv < BATT_SHUTDOWN_MV) {
        if (++shutdownReadings >= BATT_SHUTDOWN_READINGS) {
            sendStatus(mv); // last report so the timer shows why it went quiet
            powerOffLowBattery();
        }
    } else {
        shutdownReadings = 0;
    }
}

// ~1Hz short flash while the battery is low. Only touches the LED when its
// state needs to change.
static void lowBatteryLed() {
    static bool lit = false;
    if (!lowBattery) {
        if (lit) { ledOff(); lit = false; }
        return;
    }
    bool want = (millis() % 1000) < 100;
    if (want != lit) {
        lit = want;
        if (want) ledOn(); else ledOff();
    }
}

static void statusTick() {
    if (millis() - lastActivityMs >= IDLE_POWEROFF_MS) {
        ledBlink(2, 300, 300);
        powerOff();
    }

    if ((int32_t)(millis() - nextStatusMs) < 0) return;

    sendStatus(batteryReadMv());
    // Spread the remotes' heartbeats out a little so they don't all key up
    // in the same instant.
    nextStatusMs = millis() + STATUS_INTERVAL_MS + (deviceId[3] & 0x0F) * 500UL;
}

// ---- setup / loop --------------------------------------------------------------
void setup() {
    getDeviceId(deviceId);
    readRoleJumpers();

    ledOn(); // solid while booting

    if (isJudge) {
        lowPowerInitButton(PIN_START);
        lowPowerInitButton(PIN_PAUSE);
        lowPowerInitButton(PIN_TSEL);
        lowPowerInitButton(PIN_BUZZ);
    }
    lowPowerInitButton(PIN_RESET);

    if (!radioInit()) {
        ledOff();
        ledBlink(6, 100, 100);
        powerOff();
    }

    uint16_t mv = batteryReadMv();
    debugPort().print(wdtCausedReset() ? "[boot] (after watchdog reset) role=" : "[boot] role=");
    debugPort().print(myRole);
    debugPort().print(" battery=");
    debugPort().print(mv);
    debugPort().println("mV");
    if (mv < BATT_MIN_BOOT_MV) {
        ledOff();
        powerOffLowBattery();
    }

    // The press that powered the board on may still be down; its release is
    // not a command and must not start the power-off timer.
    while (powerButtonDown()) waitMs(5);
    waitMs(DEBOUNCE_MS);

    ledOff();
    waitMs(150);
    ledBlink(1, 150, 0);

    sendStatus(mv);
    nextStatusMs = millis() + STATUS_INTERVAL_MS + (deviceId[3] & 0x0F) * 500UL;
    lastActivityMs = millis();
    lowBattery = (mv < BATT_LOW_WARN_MV);
    nextBattCheckMs = millis() + BATT_CHECK_MS;

    // Started only now: radio flashing above can legitimately take longer
    // than the timeout.
    wdtStart();
}

void loop() {
    wdtFeed();
#ifdef WDT_SELFTEST
    // Bench test only (-D WDT_SELFTEST): hang 20s after boot without feeding
    // the watchdog; the board should reset and print the watchdog-reset boot line.
    if (millis() > 20000UL) {
        for (;;) {
        }
    }
#endif
    if (isJudge) {
        if (digitalRead(PIN_START) == LOW) handleButton(PIN_START, BTN_START);
        if (digitalRead(PIN_PAUSE) == LOW) handleButton(PIN_PAUSE, BTN_PAUSE);
        if (digitalRead(PIN_TSEL) == LOW) handleButton(PIN_TSEL, BTN_TIME_SEL);
        if (digitalRead(PIN_BUZZ) == LOW) handleButton(PIN_BUZZ, BTN_BUZZER);
        if (powerButtonDown()) handlePowerButton(BTN_RESET);
    } else {
        // buttonId is unused for ready/tap-out packets - the timer decides
        // ready-up vs tap-out from its own state, not from packet content.
        if (powerButtonDown()) handlePowerButton(0);
    }

    batteryMonitor();
    lowBatteryLed();
    statusTick();
    lowPowerSleep();
}
