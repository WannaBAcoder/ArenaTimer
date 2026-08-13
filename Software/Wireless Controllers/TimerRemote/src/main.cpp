#include <Arduino.h>
#include <string.h>
#include "config.h"
#include "packet.h"
#include "pairing.h"
#include "radio.h"
#include "lowpower.h"

#if defined(VARIANT_READY_REMOTE)
#include "roleConfig.h"
#endif

static uint8_t deviceId[4];

// One flag per button, set from ISR context (attachInterruptWakeup callback),
// consumed in loop(). Kept minimal - no work happens in the ISR itself.
static volatile bool wokeStart = false;
static volatile bool wokePause = false;
static volatile bool wokeReset = false;
static volatile bool wokeTsel = false;
static volatile bool wokeBuzz = false;

static void onWakeStart() { wokeStart = true; }
static void onWakePause() { wokePause = true; }
static void onWakeReset() { wokeReset = true; }
static void onWakeTsel() { wokeTsel = true; }
static void onWakeBuzz() { wokeBuzz = true; }

static void sendPacket(uint8_t role, uint8_t buttonId) {
    RemotePacket pkt;
    memcpy(pkt.deviceId, deviceId, sizeof(deviceId));
    pkt.role = role;
    pkt.buttonId = buttonId;
    radioSendPacket(reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt));
}

// Simple press-and-release handling: debounce, send once, then block until
// the button is physically released before allowing sleep again. This is a
// deliberate change from the old ESP-NOW controller firmware, which kept
// re-blasting packets every ~25ms for as long as any button was held. LoRa
// duty-cycle/airtime considerations make that pattern inappropriate here, so
// every button is single-shot per press on this firmware - except BUZZER
// (see below), which needs to feel like a continuous tone while held.
//
// The ESP32 side (Software/main/src/timerLogic.cpp's triggerBeep()) already
// extends its beep-off timer by 250ms on every BTN_BUZZER packet received,
// and lets the tone lapse if packets stop arriving - originally meant as a
// lost-signal safeguard, but it doubles as exactly the mechanism needed
// here. Resending well inside that 250ms window keeps the tone continuous
// with comfortable margin for an occasional dropped packet, at a small
// fraction of the old ESP-NOW rate.
#define BUZZER_REPEAT_MS 100

static void handleButtonPress(uint32_t pin, uint8_t role, uint8_t buttonId) {
    delay(30); // debounce
    if (digitalRead(pin) != LOW) return; // was noise, not a real press

    sendPacket(role, buttonId);

    if (buttonId == BTN_BUZZER) {
        uint32_t lastSend = millis();
        while (digitalRead(pin) == LOW) {
            if (millis() - lastSend >= BUZZER_REPEAT_MS) {
                lastSend = millis();
                sendPacket(role, buttonId);
            }
            delay(10);
        }
        return;
    }

    while (digitalRead(pin) == LOW) {
        delay(10);
    }
}

void setup() {
    getDeviceId(deviceId);
    radioInit();
    lowPowerInit();

#if defined(VARIANT_CONTROLLER)
    lowPowerAttachWakeButton(PIN_START, onWakeStart);
    lowPowerAttachWakeButton(PIN_PAUSE, onWakePause);
    lowPowerAttachWakeButton(PIN_RESET, onWakeReset);
    lowPowerAttachWakeButton(PIN_TSEL, onWakeTsel);
    lowPowerAttachWakeButton(PIN_BUZZ, onWakeBuzz);
#elif defined(VARIANT_READY_REMOTE)
    lowPowerAttachWakeButton(PIN_RESET, onWakeReset); // the ReadyRemote's one button
#else
#error "Define VARIANT_CONTROLLER or VARIANT_READY_REMOTE (see platformio.ini envs)"
#endif

}

void loop() {
#if defined(VARIANT_CONTROLLER)
    if (wokeStart) { wokeStart = false; handleButtonPress(PIN_START, ROLE_JUDGE, BTN_START); }
    if (wokePause) { wokePause = false; handleButtonPress(PIN_PAUSE, ROLE_JUDGE, BTN_PAUSE); }
    if (wokeReset) { wokeReset = false; handleButtonPress(PIN_RESET, ROLE_JUDGE, BTN_RESET); }
    if (wokeTsel) { wokeTsel = false; handleButtonPress(PIN_TSEL, ROLE_JUDGE, BTN_TIME_SEL); }
    if (wokeBuzz) { wokeBuzz = false; handleButtonPress(PIN_BUZZ, ROLE_JUDGE, BTN_BUZZER); }
#elif defined(VARIANT_READY_REMOTE)
    if (wokeReset) {
        wokeReset = false;
        uint8_t role = (READY_REMOTE_COLOR == 1) ? ROLE_RED_READY : ROLE_BLUE_READY;
        // buttonId is unused for ready/tapout packets - the timer decides
        // ready-up vs tap-out from its own state, not from packet content.
        handleButtonPress(PIN_RESET, role, 0);
    }
#endif

    lowPowerSleep();
}
