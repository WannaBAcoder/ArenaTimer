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

#endif