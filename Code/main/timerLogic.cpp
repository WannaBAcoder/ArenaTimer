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
    
    // Initial visual state
    for (int i = 0; i < BORDER_LED_COUNT; i++) setBorderLEDs(i, ORANGE);
    
    setDigit(0, 0, false);
    setDigit(0, 49, false);
    setColon();
    setDigit(countdown, 101, true);
    setDigit(0, 150, true);
    
    FastLED.show();
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

    FastLED.show();

    scrollIndex--;
    if (scrollIndex < 0) {
      scrollIndex = BORDER_LED_COUNT - 1;
      countdown--;

      if (countdown <= 0) {
        transitionToMatch();
        return;
      }

      // only draw digit if countdown > 0
      setDigit(0, 0, false);
      setDigit(0, 49, false);
      setColon();
      setDigit(countdown, 101, true);
      setDigit(0, 150, true);
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
    if (cmd == "start" && currentState != FINISHED) {
        currentState = PRE_COUNTDOWN_INIT;
        blinkState = true;
    } 
    else if (cmd == "pause" && currentState != FINISHED) {
        currentState = PAUSED;
    } 
    else if (cmd == "reset" && (currentState == FINISHED || currentState == PAUSED)) {
        blueReady = redReady = false;
        currentState = IDLE;
        current_time = countdown_time;
        blinkState = true;

        setBorder();
        updateClient();
        updateLEDs();
        FastLED.show();
    } 
    else if (cmd == "switch" && currentState != RUNNING) {
        countdown_time = (countdown_time == 120) ? 180 : 120;
        current_time = countdown_time;
        updateClient();
        updateLEDs();
    }
}

void setTeamReady(String team) {
    if (team == "Blue") {
        blueReady = true;
        for (int i = 0; i < BORDER_LED_COUNT/2; i++) {
            setBorderLEDs(i, CRGB::Blue);
        }
    } else if (team == "Red") {
        redReady = true;
        for (int i = BORDER_LED_COUNT/2; i < BORDER_LED_COUNT; i++) {
            setBorderLEDs(i, CRGB::Red);
        }
    }
    needsLEDUpdate = true;
}