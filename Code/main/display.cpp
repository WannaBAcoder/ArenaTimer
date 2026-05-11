#include "Display.h"

extern bool needsLEDUpdate;

// Define the physical arrays here, since they are used by the Display functions
CRGB digit_physical[PHYSICAL_STRIP_LEN];
CRGB border_physical[PHYSICAL_STRIP_LEN];

void initDisplay() {
    FastLED.addLeds<NEOPIXEL, DIGIT_PIN>(digit_physical, PHYSICAL_STRIP_LEN);
    FastLED.addLeds<NEOPIXEL, BORDER_PIN>(border_physical, PHYSICAL_STRIP_LEN);
}

void setDigit(int digit, int offset, bool inverted) {
    static const uint8_t digitMap[10][7] = {
        {1, 1, 1, 1, 1, 1, 0}, {0, 1, 1, 0, 0, 0, 0}, {1, 1, 0, 1, 1, 0, 1},
        {1, 1, 1, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 1, 1}, {1, 0, 1, 1, 0, 1, 1},
        {1, 0, 1, 1, 1, 1, 1}, {1, 1, 1, 0, 0, 0, 0}, {1, 1, 1, 1, 1, 1, 1},
        {1, 1, 1, 1, 0, 1, 1}
    };
    static const uint8_t invertedMap[7] = {3, 4, 5, 0, 1, 2, 6};

    for (int i = 0; i < 7; i++) {
        int segmentIndex = inverted ? invertedMap[i] : i;
        int ledIndex = offset + segmentIndex * 7;
        for (int j = 0; j < 7; j++) {
            // Replace CRGB::Red with digitColor
            setDigitLEDs(ledIndex + j, digitMap[digit][i] ? digitColor : CRGB::Black);
        }
    }
}

void setColon() {
    for (int i = 0; i < 3; i++) {
        setDigitLEDs(98 + i, digitColor);
        setDigitLEDs(199 + i, digitColor);
    }
}
void updateLEDs() {
    int minutes = current_time / 60;
    int seconds = current_time % 60;
    setDigit(minutes / 10, 0, false);
    setDigit(minutes % 10, 49, false);
    setColon();
    setDigit(seconds / 10, 150, true);
    setDigit(seconds % 10, 101, true);
    
    needsLEDUpdate = true;
}

void setBorder() {
    if(!readyRequired || (redReady && blueReady)) {
        for (int i = 0; i < BORDER_LED_COUNT/2; i++) 
          setBorderLEDs(i, CRGB::Blue);
        for (int i = BORDER_LED_COUNT/2; i < BORDER_LED_COUNT ; i++) 
          setBorderLEDs(i, CRGB::Red);
    } else {
        for (int i = 0; i < BORDER_LED_COUNT; i++) setBorderLEDs(i, CRGB(127,127,127));
    }
    needsLEDUpdate = true;
}

void setDigitLEDs(int index, CRGB color) {
    if (index < HALF_DIGIT) {
        digit_physical[index] = color;
    } else {
        int mapped = index - HALF_DIGIT;
        if (mapped < (PHYSICAL_STRIP_LEN - HALF_BORDER)) {
            border_physical[HALF_BORDER + mapped] = color;
        }
    }
}

void setBorderLEDs(int index, CRGB color) {
    if (index < HALF_BORDER) {
        border_physical[index] = color;
    } else {
        int mapped = index - HALF_BORDER;
        if (mapped < (PHYSICAL_STRIP_LEN - HALF_DIGIT)) {
            digit_physical[HALF_DIGIT + mapped] = color;
        }
    }
}