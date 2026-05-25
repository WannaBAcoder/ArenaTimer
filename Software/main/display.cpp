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
    
    // Segment rotation: Maps old index to new physical location when upside down
    // Standard: 0:A, 1:B, 2:C, 3:D, 4:E, 5:F, 6:G
    // Flipped:  3:D, 4:E, 5:F, 0:A, 1:B, 2:C, 6:G
    static const uint8_t flipSegMap[7] = {3, 4, 5, 0, 1, 2, 6};
    
    static const uint8_t invertedMap[7] = {3, 4, 5, 0, 1, 2, 6};

    for (int i = 0; i < 7; i++) {
        int segmentIndex = inverted ? invertedMap[i] : i;
        
        // Apply the 180-degree rotation to the segment position
        if (displayInverted) {
            segmentIndex = flipSegMap[segmentIndex];
        }

        int ledIndex = offset + segmentIndex * 7;
        
        for (int j = 0; j < 7; j++) {
            int pixelOffset = j;
            
            // Reverses the order of the 7 LEDs inside the segment
            if (displayInverted) {
                pixelOffset = 6 - j; 
            }
            
            setDigitLEDs(ledIndex + pixelOffset, digitMap[digit][i] ? digitColor : CRGB::Black);
        }
    }
}

void setColon() {
    for (int i = 0; i < 3; i++) {
        // If inverted, we offset the colon slightly or reverse the sub-index
        int idx1 = displayInverted ? (98 + (2 - i)) : (98 + i);
        int idx2 = displayInverted ? (199 + (2 - i)) : (199 + i);
        
        setDigitLEDs(idx1, digitColor);
        setDigitLEDs(idx2, digitColor);
    }
}

void updateLEDs() {
    int minutes = current_time / 60;
    int seconds = current_time % 60;
    
    if (!displayInverted) {
        setDigit(minutes / 10, 0, false);   // Digit 1
        setDigit(minutes % 10, 49, false);  // Digit 2
        setDigit(seconds / 10, 150, true);  // Digit 3
        setDigit(seconds % 10, 101, true);  // Digit 4
    } else {
        // Swap positions and inversion status
        setDigit(seconds % 10, 0, false);   // Was Digit 4
        setDigit(seconds / 10, 49, false);  // Was Digit 3
        setDigit(minutes % 10, 150, true);  // Was Digit 2
        setDigit(minutes / 10, 101, true);  // Was Digit 1
    }
    setColon();
    needsLEDUpdate = true;
}

void setBorder() {
    bool flip = displayInverted;
    if(!readyRequired || (redReady && blueReady)) {
        // Swap Blue/Red ranges if inverted
        int startBlue = flip ? BORDER_LED_COUNT/2 : 0;
        int endBlue = flip ? BORDER_LED_COUNT : BORDER_LED_COUNT/2;
        int startRed = flip ? 0 : BORDER_LED_COUNT/2;
        int endRed = flip ? BORDER_LED_COUNT/2 : BORDER_LED_COUNT;

        for (int i = startBlue; i < endBlue; i++) setBorderLEDs(i, CRGB::Blue);
        for (int i = startRed; i < endRed; i++) setBorderLEDs(i, CRGB::Red);
    } else {
        for (int i = 0; i < BORDER_LED_COUNT; i++) setBorderLEDs(i, CRGB(255, 255, 255));
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