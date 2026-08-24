#ifndef DISPLAY_H
#define DISPLAY_H

#include <FastLED.h>
#include "Config.h"

// Function prototypes
void initDisplay();
void setDigit(int digit, int offset, bool inverted);
void setChar(char c, int offset, bool inverted);
void setColon();
void updateLEDs();
void setBorder();
void setDigitLEDs(int index, CRGB color);
void setBorderLEDs(int index, CRGB color);
void applyDoubleSidedMirror();
void showLeds();

// millis() timestamp of the last showLeds() call. FastLED.show() is
// asynchronous on ESP32 - it kicks off an RMT transfer in the background
// and returns before the frame finishes clocking out. Anything that blocks
// or otherwise starves the RMT refill ISR while a transfer is still in
// flight corrupts that frame (confirmed on a scope during the power-on
// rainbow investigation). Code that does blocking work - e.g. the WiFi
// reconnect check in loop() - should wait out a short window after this
// before running, rather than risk landing mid-transfer.
extern uint32_t lastLedShowMillis;


#endif