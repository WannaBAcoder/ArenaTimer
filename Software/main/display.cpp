#include "Display.h"

extern bool needsLEDUpdate;
extern bool isDoubleSided;

SemaphoreHandle_t displayMutex = NULL;

// Define the physical arrays here, since they are used by the Display functions
CRGB digit_physical[DOUBLE_STRIP_LEN];
CRGB border_physical[DOUBLE_STRIP_LEN];

int mirrorOffsetIndex = 0;

void initDisplay() {
    if (isDoubleSided) {
        FastLED.addLeds<NEOPIXEL, DIGIT_PIN>(digit_physical, DOUBLE_STRIP_LEN);
        FastLED.addLeds<NEOPIXEL, BORDER_PIN>(border_physical, DOUBLE_STRIP_LEN);
    } else {
        FastLED.addLeds<NEOPIXEL, DIGIT_PIN>(digit_physical, PHYSICAL_STRIP_LEN);
        FastLED.addLeds<NEOPIXEL, BORDER_PIN>(border_physical, PHYSICAL_STRIP_LEN);
    }
}

void lockDisplay() {
    if (displayMutex != NULL) {
        xSemaphoreTake(displayMutex, portMAX_DELAY);
    }
}

void unlockDisplay() {
    if (displayMutex != NULL) {
        xSemaphoreGive(displayMutex);
    }
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

void setChar(char c, int offset, bool inverted) {
    // 7-segment bit pattern font map (A, B, C, D, E, F, G)
    uint8_t segments = 0;
    switch (c) {
        case '-': segments = 0b0000001; break; // Only turn on Center Segment G
        case 't': segments = 0b0001111; break; // D, E, F, G
        case 'A': segments = 0b1110111; break; // A, B, C, E, F, G
        case 'P': segments = 0b1100111; break; // A, B, E, F, G
        case 'O': segments = 0b1111110; break; // A, B, C, D, E, F (Capital O)
        case 'U': segments = 0b0111110; break; // B, C, D, E, F (Capital U)
        default:  segments = 0b0000000; break; // Blank space
    }

    static const uint8_t flipSegMap[7] = {3, 4, 5, 0, 1, 2, 6};
    static const uint8_t invertedMap[7] = {3, 4, 5, 0, 1, 2, 6};

    for (int i = 0; i < 7; i++) {
        int segmentIndex = inverted ? invertedMap[i] : i;
        
        if (displayInverted) {
            segmentIndex = flipSegMap[segmentIndex];
        }

        int ledIndex = offset + segmentIndex * 7;
        
        for (int j = 0; j < 7; j++) {
            int pixelOffset = j;
            if (displayInverted) {
                pixelOffset = 6 - j; 
            }
            
            // Check the specific bit state for this segment
            bool bitActive = (segments >> (6 - i)) & 0x01;
            setDigitLEDs(ledIndex + pixelOffset, bitActive ? digitColor : CRGB::Black);
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
    
    // Force a non-white accent color if in Clock Mode, regardless of ready constraints
    if (currentState == CLOCK_MODE) {
        for (int i = 0; i < BORDER_LED_COUNT; i++) {
            setBorderLEDs(i, ORANGE); 
        }
    } 
    else if (!readyRequired || (redReady && blueReady)) {
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
        digit_physical[mirrorOffsetIndex + index] = color;
    } else {
        int mapped = index - HALF_DIGIT;
        if (mapped < (PHYSICAL_STRIP_LEN - HALF_BORDER)) {
            border_physical[mirrorOffsetIndex + HALF_BORDER + mapped] = color;
        }
    }
}

void setBorderLEDs(int index, CRGB color) {
    if (index < HALF_BORDER) {
        border_physical[mirrorOffsetIndex + index] = color;
    } else {
        int mapped = index - HALF_BORDER;
        if (mapped < (PHYSICAL_STRIP_LEN - HALF_DIGIT)) {
            digit_physical[mirrorOffsetIndex + HALF_DIGIT + mapped] = color;
        }
    }
}

//this one is a doozy, handles drawing side B but flipping the border sides
void applyDoubleSidedMirror() {
    if (!isDoubleSided) return;

    // 1. Tank-proof 1:1 raw parallel clone baseline
    for (int i = 0; i < PHYSICAL_STRIP_LEN; i++) {
        digit_physical[PHYSICAL_STRIP_LEN + i] = digit_physical[i];
        border_physical[PHYSICAL_STRIP_LEN + i] = border_physical[i];
    }

    // 2. Force the 180-degree border flip on Side B during active match states
    if (currentState == RUNNING || currentState == PAUSED || currentState == IDLE) {
        
        // Loop through the first half of the logical border (70 pixels)
        for (int i = 0; i < HALF_BORDER; i++) {
            int idxA = i;                  // First half (e.g., 0-69)
            int idxB = HALF_BORDER + i;    // Opposite half (e.g., 70-139)

            // Calculate exact physical source locations on Side A
            int srcPhysIdxA_digit = -1, srcPhysIdxA_border = -1;
            int srcPhysIdxB_digit = -1, srcPhysIdxB_border = -1;

            // Resolve Side A physical slots for Quadrant A using your exact driver mapping rules
            if (idxA < HALF_BORDER) {
                srcPhysIdxA_border = idxA;
            } else {
                srcPhysIdxA_digit = HALF_DIGIT + (idxA - HALF_BORDER);
            }

            // Resolve Side A physical slots for Quadrant B
            if (idxB < HALF_BORDER) {
                srcPhysIdxB_border = idxB;
            } else {
                srcPhysIdxB_digit = HALF_DIGIT + (idxB - HALF_BORDER);
            }

            // Calculate exact physical destination locations on Side B
            int destPhysIdxA_digit = -1, destPhysIdxA_border = -1;
            int destPhysIdxB_digit = -1, destPhysIdxB_border = -1;

            if (idxA < HALF_BORDER) {
                destPhysIdxA_border = PHYSICAL_STRIP_LEN + idxA;
            } else {
                destPhysIdxA_digit = PHYSICAL_STRIP_LEN + HALF_DIGIT + (idxA - HALF_BORDER);
            }

            if (idxB < HALF_BORDER) {
                destPhysIdxB_border = PHYSICAL_STRIP_LEN + idxB;
            } else {
                destPhysIdxB_digit = PHYSICAL_STRIP_LEN + HALF_DIGIT + (idxB - HALF_BORDER);
            }

            // Overwrite Side B Side A (Quadrant A Destination gets Side A Quadrant B Source)
            if (destPhysIdxA_border != -1 && srcPhysIdxB_border != -1) {
                border_physical[destPhysIdxA_border] = border_physical[srcPhysIdxB_border];
            } else if (destPhysIdxA_digit != -1 && srcPhysIdxB_digit != -1) {
                digit_physical[destPhysIdxA_digit] = digit_physical[srcPhysIdxB_digit];
            } else if (destPhysIdxA_border != -1 && srcPhysIdxB_digit != -1) {
                border_physical[destPhysIdxA_border] = digit_physical[srcPhysIdxB_digit];
            } else if (destPhysIdxA_digit != -1 && srcPhysIdxB_border != -1) {
                digit_physical[destPhysIdxA_digit] = border_physical[srcPhysIdxB_border];
            }

            // Overwrite Side B Side B (Quadrant B Destination gets Side A Quadrant A Source)
            if (destPhysIdxB_border != -1 && srcPhysIdxA_border != -1) {
                border_physical[destPhysIdxB_border] = border_physical[srcPhysIdxA_border];
            } else if (destPhysIdxB_digit != -1 && srcPhysIdxA_digit != -1) {
                digit_physical[destPhysIdxB_digit] = digit_physical[srcPhysIdxA_digit];
            } else if (destPhysIdxB_border != -1 && srcPhysIdxA_digit != -1) {
                border_physical[destPhysIdxB_border] = digit_physical[srcPhysIdxA_digit];
            } else if (destPhysIdxB_digit != -1 && srcPhysIdxA_border != -1) {
                digit_physical[destPhysIdxB_digit] = border_physical[srcPhysIdxA_border];
            }
        }
    }
}