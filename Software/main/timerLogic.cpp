#include "TimerLogic.h"

// Reference globals defined in main.ino
extern int current_time;
extern int countdown_time;
extern int currentState;
extern int countdown;
extern int scrollIndex;
extern unsigned long lastScrollTime;
extern unsigned long lastCountdownTime;
extern bool readyRequired;
extern bool blueReady;
extern bool redReady;
extern volatile bool needsLEDUpdate;

// Debounce variables for each button
unsigned long lastDebounceTimeStart = 0;
unsigned long lastDebounceTimePause = 0;
unsigned long lastDebounceTimeReset = 0;
unsigned long lastDebounceTimeTimeSel = 0;
unsigned long lastDebounceTimeBlue = 0;
unsigned long lastDebounceTimeRed = 0;


bool blinkState = true;
unsigned long lastBlinkTime = 0;
const unsigned long blinkInterval = 500; // Blink every 500ms

void handleConnectingAnimation() {

  static uint8_t head = 0;
  static unsigned long lastAnimTime = 0;
  const uint8_t animSpeed = 30; // Milliseconds per frame
  const uint8_t tailLength = 15;

  if (millis() - lastAnimTime >= animSpeed) {
    lastAnimTime = millis();

    // Clear the border
    for (int i = 0; i < BORDER_LED_COUNT; i++) {
      setBorderLEDs(i, CRGB::Black);
    }

    // Draw the "snake"
    for (int i = 0; i < tailLength; i++) {
      int pos = (head - i + BORDER_LED_COUNT) % BORDER_LED_COUNT;
      // Fade the tail
      uint8_t brightness = map(i, 0, tailLength, 255, 0);
      setBorderLEDs(pos, ORANGE.fadeToBlackBy(255 - brightness)); // Cyan snake
    }

    head = (head + 1) % BORDER_LED_COUNT;
    needsLEDUpdate = true;
  }
}

void checkButtons() {
  unsigned long currentMillis = millis();

  // Start button
  if (digitalRead(START_BTN) == LOW && currentMillis - lastDebounceTimeStart > debounceDelay) {
    processCommand("start");
    lastDebounceTimeStart = currentMillis;  // Update debounce time
  }

  // Pause button
  if (digitalRead(PAUSE_BTN) == LOW && currentMillis - lastDebounceTimePause > debounceDelay) {
    processCommand("pause");
    lastDebounceTimePause = currentMillis;
  }

  // Reset button
  if (digitalRead(RESET_BTN) == LOW && (currentMillis - lastDebounceTimeReset > debounceDelay) 
                  && (currentState == FINISHED || currentState == PAUSED)) {
    processCommand("reset");
 
    lastDebounceTimeReset = currentMillis;
  }

  // Time select switch
  if (digitalRead(TIME_SEL_SW) == LOW && (currentMillis - lastDebounceTimeTimeSel > debounceDelay)  
                  && currentState != RUNNING) {
    processCommand("switch");
    lastDebounceTimeTimeSel = currentMillis;
  }

  if(readyRequired) {
    // Blue Ready button
    if (digitalRead(BLUE_BTN) == LOW && (currentMillis - lastDebounceTimeBlue > debounceDelay)) {
      setTeamReady("Blue");
      lastDebounceTimeBlue = millis();
    }

    // Red Ready button
    if (digitalRead(RED_BTN) == LOW && (currentMillis - lastDebounceTimeRed > debounceDelay)) {
      setTeamReady("Red");
      lastDebounceTimeRed = millis();
    }
  }
}

void handlePausedBlink() {
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastBlinkTime >= blinkInterval) {
    lastBlinkTime = currentMillis;
    blinkState = !blinkState;
    
    if (blinkState) {
        updateLEDs(); // Redraws the digits based on current_time
    } else {
      // Clear only the digit LEDs (0 to 201)
      for (int i = 0; i < DIGIT_LED_COUNT; i++) {
          setDigitLEDs(i, CRGB::Black);
      }
    }
    needsLEDUpdate = true; // Signal the Task to call FastLED.show()
  }
}

void startPreCountdown() {
  countdown = 3;
  scrollIndex = BORDER_LED_COUNT - 1;
  lastScrollTime = millis();
    
  // Fill border with warning color
  for (int i = 0; i < BORDER_LED_COUNT; i++) setBorderLEDs(i, ORANGE);
  
  // Digit Logic: Swap physical locations if display is inverted
  if (!displayInverted) {
      // Normal: "0" on the left, Countdown digit on the right
      setDigit(0, 0, false);         // Left Minutes
      setDigit(0, 49, false);        // Right Minutes
      setColon();
      setDigit(countdown, 101, true); // Right Seconds (The countdown digit)
      setDigit(0, 150, true);        // Left Seconds
  } else {
      // Inverted: The physical "Right Seconds" (index 101) is now on the LEFT.
      // We put the "0" there and put the countdown digit at index 0.
      setDigit(countdown, 0, false);  // Physically Rightmost now
      setDigit(0, 49, false);
      setColon();
      setDigit(0, 101, true);         // Physically Leftmost now
      setDigit(0, 150, true);
  }
  
  needsLEDUpdate = true;
  currentState = PRE_COUNTDOWN_LOOP; // Move to the animation loop
}

void handlePreCountdownAnimation() {
  unsigned long currentMillis = millis();
  if (currentMillis - lastScrollTime >= scrollInterval) {
    lastScrollTime = currentMillis;

    // Fill from 0 to scrollIndex with ORANGE
    for (int i = 0; i < BORDER_LED_COUNT; i++) {
      if (i >= scrollIndex) {
        setBorderLEDs(i, ORANGE);
      } 
      else {
        setBorderLEDs(i, CRGB::Black);
      }
    }

    needsLEDUpdate = true;

    scrollIndex--;
    if (scrollIndex < 0) {
      scrollIndex = BORDER_LED_COUNT - 1;
      countdown--;

      if (countdown <= 0) {
        transitionToMatch();
        return;
      }

      if (!displayInverted) {
          setDigit(0, 0, false);
          setDigit(0, 49, false);
          setColon();
          setDigit(countdown, 101, true);
          setDigit(0, 150, true);
      } else {
          setDigit(countdown, 0, false);
          setDigit(0, 49, false);
          setColon();
          setDigit(0, 101, true);
          setDigit(0, 150, true);
      }

      needsLEDUpdate = true;
    }
  }
}

void updateTimer() {
  unsigned long currentMillis = millis();

  // Main timer counting down
  if (current_time > 0) {
    // Countdown every 1 second
    if (currentMillis - lastCountdownTime >= 1000) {
      lastCountdownTime = currentMillis;
      current_time--;
      
      updateClient();  // Reset to 2:00 (or 3:00)
      updateLEDs();
    }
  }

  else {
    currentState = FINISHED;
  }
}

void transitionToMatch() {
  // Only reset the time if we aren't in the middle of a match
  // If current_time is already less than countdown_time, we leave it alone
  if (current_time <= 0 || current_time == countdown_time) {
      current_time = countdown_time;
  }

  lastCountdownTime = millis();
  
  setBorder(); 
  updateLEDs();
  updateClient();
  
  currentState = RUNNING;
}

void processCommand(String cmd) {
  if (cmd == "start") {
    if (currentState == IDLE || currentState == PAUSED) {
        
      if (readyRequired && (!redReady || !blueReady)) {
          Serial.println("[LOCKOUT] Start denied: Drivers not ready.");
          return; 
      }
      
      currentState = PRE_COUNTDOWN_INIT;
      blinkState = true;
      return; // Exit early since we handled the command
    }
    
    Serial.println("[LOCKOUT] Start ignored: System not in IDLE or PAUSED.");
    return;
  }
  else if (cmd == "pause" && currentState == RUNNING) {
    currentState = PAUSED;
  } 
  else if (cmd == "reset" && (currentState == FINISHED || currentState == PAUSED || currentState == CLOCK_MODE)) {
    blueReady = redReady = false;
    currentState = IDLE;
    current_time = countdown_time;
    blinkState = true;

    setBorder();
    updateClient();
    updateLEDs();
  } 
  else if (cmd == "switch" && (currentState == IDLE || currentState == FINISHED)) {
    countdown_time = (countdown_time == 120) ? 180 : 120;
    current_time = countdown_time;

    bool newTimeSelState = (countdown_time == 180);

    preferences.begin("settings", false);
    preferences.putBool("timeSelState", newTimeSelState);
    preferences.end();

    updateClient();
    updateLEDs();
  }
}

void setTeamReady(String team) {
  bool flip = displayInverted;
  if (team == "Blue") {
      blueReady = true;
      int start = flip ? BORDER_LED_COUNT/2 : 0;
      int end = flip ? BORDER_LED_COUNT : BORDER_LED_COUNT/2;
      for (int i = start; i < end; i++) setBorderLEDs(i, CRGB::Blue);
  } else if (team == "Red") {
      redReady = true;
      int start = flip ? 0 : BORDER_LED_COUNT/2;
      int end = flip ? BORDER_LED_COUNT/2 : BORDER_LED_COUNT;
      for (int i = start; i < end; i++) setBorderLEDs(i, CRGB::Red);
  }
  needsLEDUpdate = true;
}

void handleClockMode() {
  static unsigned long lastClockUpdate = 0;
  if (millis() - lastClockUpdate < 1000 && !needsLEDUpdate) return;
  lastClockUpdate = millis();

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;

  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;

  if (hour == 0) hour = 12;
  if (hour > 12) hour -= 12;

  if (!displayInverted) {
      // Normal: Hours on Left, Minutes on Right
      setDigit(hour / 10, 0, false);
      setDigit(hour % 10, 49, false);
      setColon();
      setDigit(minute / 10, 150, true);
      setDigit(minute % 10, 101, true);
  } else {
      // Inverted: Minutes on Left (Indices 0/49), Hours on Right (Indices 150/101)
      setDigit(minute % 10, 0, false);
      setDigit(minute / 10, 49, false);
      setColon();
      setDigit(hour % 10, 150, true);
      setDigit(hour / 10, 101, true);
  }
  
  needsLEDUpdate = true;
}