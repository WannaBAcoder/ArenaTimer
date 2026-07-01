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
extern bool tapoutInitiatorIsBlue;

bool buzzState = 0;
bool relayState = 0;

// Debounce variables for each button
unsigned long lastDebounceTimeStart = 0;
unsigned long lastDebounceTimePause = 0;
unsigned long lastDebounceTimeReset = 0;
unsigned long lastDebounceTimeTimeSel = 0;
unsigned long lastDebounceTimeBlue = 0;
unsigned long lastDebounceTimeRed = 0;

extern bool audioEnabled;
extern uint8_t audioOutputSelect;
extern uint32_t beepEndTime;
extern bool beepActive;

bool blinkState = true;
unsigned long lastBlinkTime = 0;
const unsigned long blinkInterval = 500; // Blink every 500ms

extern TaskHandle_t TimerTaskHandle;

unsigned long lastPatternTick = 0;
uint8_t patternStep = 0;
bool rapidPatternActive = false;

int tapoutScrollPos = 0;
bool tapoutAudioTriggeredThisCycle = false;

extern CRGB digit_physical[];
extern CRGB border_physical[];

void handleConnectingAnimation() {
  static unsigned long lastAnimTime = 0;
  static bool borderOn = true;
  const uint16_t blinkInterval = 500; // Blink every 500ms

  if (millis() - lastAnimTime >= 30) {
    lastAnimTime = millis();

    if (millis() % (blinkInterval * 2) < blinkInterval) {
      borderOn = true;
    } else {
      borderOn = false;
    }

    for (int i = 0; i < DOUBLE_STRIP_LEN; i++) {
      digit_physical[i] = CRGB::Black;
      border_physical[i] = CRGB::Black;
    }

    CRGB currentBorderColor = borderOn ? ORANGE : CRGB::Black;
    for (int i = 0; i < BORDER_LED_COUNT; i++) {
      setBorderLEDs(i, currentBorderColor);
    }

    setChar('-', 0, displayInverted); 
    setChar('-', 49, displayInverted);  
    setChar('-', 150, displayInverted); 
    setChar('-', 101, displayInverted); 
    setColon();                         

    needsLEDUpdate = true;
  }
}

void checkButtons() {
  if (Serial.available() > 0) { 
    char rc = Serial.read(); 
    
    if (rc == '\n' || rc == '\r') return;

    switch (rc) {
      case 's': 
        Serial.println("[SERIAL CONTROL] Start Pressed");
        processCommand("start");
        break;
      case 'p': 
        Serial.println("[SERIAL CONTROL] Pause Pressed");
        processCommand("pause");
        break;
      case 'x': 
        Serial.println("[SERIAL CONTROL] Reset Pressed");
        processCommand("reset");
        break;
      case 't': 
        Serial.println("[SERIAL CONTROL] Time Selection Toggle Pressed");
        processCommand("switch");
        break;
      case 'r': 
        Serial.println("[SERIAL TEST] Red Button Pressed"); 
        if (currentState == RUNNING) { 
            processCommand("tapoutRed");
        } else if (readyRequired) { 
            setTeamReady("Red"); 
        }
        break;
      case '1': 
        Serial.println("[SERIAL TEST] Buzzer toggle"); 
        buzzState = !buzzState;
        if(buzzState)
          tone(BUZZ_PIN, 2000);
        else
          noTone(BUZZ_PIN);
      break;
      case '2': 
        Serial.println("[SERIAL TEST] Relay toggle"); 
        relayState = !relayState;
        digitalWrite(RELAY_PIN, relayState);
      break;
      case 'b': 
        Serial.println("[SERIAL TEST] Blue Button Pressed"); 
        if (currentState == RUNNING) { 
            processCommand("tapoutBlue");
        } else if (readyRequired) { 
            setTeamReady("Blue"); 
        }
        break;
      default:
        Serial.printf("[SERIAL ALERT] Unknown hotkey instruction: '%c'\n", rc);
        break;
    }
  }
}

void handlePausedBlink() {
  unsigned long currentMillis = millis();

  if (currentMillis - lastBlinkTime >= blinkInterval) {
    lastBlinkTime = currentMillis;
    blinkState = !blinkState;
    
    if (blinkState) {
        updateLEDs(); 
    } else {
      for (int i = 0; i < DIGIT_LED_COUNT; i++) {
          setDigitLEDs(i, CRGB::Black);
      }
    }
  }
  needsLEDUpdate = true; 
}

void startPreCountdown() {
  countdown = 3;
  scrollIndex = BORDER_LED_COUNT - 1;
  lastScrollTime = millis();
    
  triggerBeep(150);  

  for (int i = 0; i < BORDER_LED_COUNT; i++) setBorderLEDs(i, ORANGE);
  
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
  currentState = PRE_COUNTDOWN_LOOP; 
  return;
}

void handlePreCountdownAnimation() {
  unsigned long currentMillis = millis();
  
  // Use a stable integer approximation for the 140 LED sweep (1000 / 140 = ~7.14ms)
  unsigned long targetStep = 8; 

  if (currentMillis - lastScrollTime >= targetStep) {
    // Calculate exactly how many discrete steps occurred in this 20ms window
    unsigned long elapsed = currentMillis - lastScrollTime;
    int stepsToMove = elapsed / targetStep;

    // Advance the tracking clock only by the time we are actually accounting for
    lastScrollTime += (stepsToMove * targetStep);

    // Apply those discrete steps to the border index
    for (int i = 0; i < stepsToMove; i++) {
      scrollIndex--;

      if (scrollIndex < 0) {
        scrollIndex = BORDER_LED_COUNT - 1;
        countdown--;

        if (countdown <= 0) {
          transitionToMatch();
          return;
        }

        triggerBeep(150);

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
      }
    }

    // Draw the final calculated positions to the physical array once
    for (int i = 0; i < BORDER_LED_COUNT; i++) {
      if (i >= scrollIndex) {
        setBorderLEDs(i, ORANGE);
      } 
      else {
        setBorderLEDs(i, CRGB::Black);
      }
    }
    needsLEDUpdate = true; 
  }
}
void updateTimer() {
  unsigned long currentMillis = millis();

  if (current_time > 0) {
    if (currentMillis - lastCountdownTime >= 1000) {
      lastCountdownTime = currentMillis;
      current_time--;
      
      updateClient();  
      updateLEDs();
    }
  }
  else {
    currentState = FINISHED;
    triggerBeep(1200);
    current_time = 0;

    for (int i = 0; i < BORDER_LED_COUNT; i++) setBorderLEDs(i, CRGB::Black);
    for (int i = 0; i < DIGIT_LED_COUNT; i++) setDigitLEDs(i, CRGB::Black);
    
    updateLEDs();

    for (int i = 0; i < BORDER_LED_COUNT; i++) {
      setBorderLEDs(i, CRGB::Green);
    }
    
    applyDoubleSidedMirror();
    FastLED.show();
    needsLEDUpdate = false;
  }
}

void transitionToMatch() {
  if (current_time <= 0 || current_time == countdown_time) {
      current_time = countdown_time;
  }

  lastCountdownTime = millis();
  triggerBeep(1000);
  
  setBorder(); 
  updateLEDs();
  updateClient();
  
  currentState = RUNNING;
}

void processCommand(String cmd) {
  Serial.printf("[ENGINE DEBUG] processCommand received tracking request: '%s' | Current State is: %d\n", cmd.c_str(), currentState);

  if (cmd == "start") { 
    if (currentState == IDLE || currentState == PAUSED) { 
      if (readyRequired && (!redReady || !blueReady)) { 
          Serial.println("[LOCKOUT] Start denied: Drivers not ready."); 
          return; 
      }
      currentState = PRE_COUNTDOWN_INIT; 
      blinkState = true; 
      return; 
    }
    Serial.println("[LOCKOUT] Start ignored: System not in IDLE or PAUSED."); 
    return; 
  }
  else if (cmd == "pause" && currentState == RUNNING) { 
    lockDisplay();
    currentState = PAUSED; 
    lastBlinkTime = 0; 
    blinkState = true;
    
    updateLEDs();
    setBorder();
    needsLEDUpdate = true;
    unlockDisplay();

    triggerBeep(300);
    updateClient();
  } 

  else if (cmd == "reset") { 
    if (currentState == FINISHED || currentState == PAUSED || currentState == CLOCK_MODE || currentState == TAPOUT || currentState == IDLE) { 
        Serial.println("[SYSTEM] Executing deep display flush and state reset...");
        
        rapidPatternActive = false;
        patternStep = 0;
        noTone(BUZZ_PIN);
        digitalWrite(RELAY_PIN, LOW);

        blueReady = redReady = false; 
        currentState = IDLE; 
        current_time = countdown_time; 
        blinkState = true; 
        tapoutScrollPos = 0;
        tapoutAudioTriggeredThisCycle = false;
        lockDisplay();
        setBorder(); 
        updateLEDs(); 
        updateClient();
        FastLED.show();
        needsLEDUpdate = false;
        unlockDisplay();
    }
  } 
  else if ((cmd == "tapoutRed" || cmd == "tapoutBlue") && currentState == RUNNING) {
    if (tapoutEnabled) {
        tapoutInitiatorIsBlue = (cmd == "tapoutBlue");
        currentState = TAPOUT;

        Serial.printf("[MATCH EVENT] TAPOUT ENFORCED BY %s!\n", tapoutInitiatorIsBlue ? "BLUE" : "RED");
        updateClient();

        tapoutScrollPos = 0;

    } else {
        Serial.printf("[SYSTEM IGNORE] %s pressed button, but Tapouts are DISABLED.\n", (cmd == "tapoutBlue") ? "Blue" : "Red");
    }
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
  else if (cmd == "clockOff") {
    Serial.println("[PERSISTENCE] Clock Mode toggled OFF via Web UI. Updating flash preference...");
    
    preferences.begin("settings", false);
    preferences.putBool("clockActive", false);
    preferences.end();

    currentState = IDLE;
    current_time = countdown_time;
    
    setBorder();
    updateLEDs();
    updateClient();
    FastLED.show();
  }
}

void setTeamReady(String team) {
  bool flip = displayInverted;
  lockDisplay();
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
  unlockDisplay();
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
      setDigit(hour / 10, 0, false); 
      setDigit(hour % 10, 49, false); 
      setColon(); 
      setDigit(minute / 10, 150, true); 
      setDigit(minute % 10, 101, true); 
  } else {
      setDigit(minute % 10, 0, false); 
      setDigit(minute / 10, 49, false); 
      setColon(); 
      setDigit(hour % 10, 150, true); 
      setDigit(hour / 10, 101, true); 
  }
  needsLEDUpdate = true;
}

void handleTapoutAnimation() {
  static unsigned long lastTapoutUpdate = 0;
  static bool flashToggle = false;
    
  unsigned long currentMillis = millis();
  CRGB teamColor = tapoutInitiatorIsBlue ? CRGB::Blue : CRGB::Red;
  
  if (currentMillis - lastTapoutUpdate >= 275) {
    lastTapoutUpdate = currentMillis;
    flashToggle = !flashToggle;
    
    CRGB borderFlashColor = flashToggle ? teamColor : ORANGE;
    for (int i = 0; i < BORDER_LED_COUNT; i++) {
      setBorderLEDs(i, borderFlashColor);
    }
    needsLEDUpdate = true;
  }

  static unsigned long lastScrollUpdate = 0;
  if (currentMillis - lastScrollUpdate >= 275) {
    lastScrollUpdate = currentMillis;

    const char marqueeText[] = "    tAP OUt    ";
    const int textLength = 15;

    for (int i = 0; i < DIGIT_LED_COUNT; i++) {
      setDigitLEDs(i, CRGB::Black);
    }

    char d1 = marqueeText[tapoutScrollPos];
    char d2 = marqueeText[tapoutScrollPos + 1];
    char d3 = marqueeText[tapoutScrollPos + 2];
    char d4 = marqueeText[tapoutScrollPos + 3];

    CRGB originalColor = digitColor;
    digitColor = teamColor;

    if (!displayInverted) {
      setChar(d1, 0, false);
      setChar(d2, 49, false);
      setChar(d3, 150, true);
      setChar(d4, 101, true);
    } else {
      setChar(d4, 0, false);
      setChar(d3, 49, false);
      setChar(d2, 150, true);
      setChar(d1, 101, true);
    }

    digitColor = originalColor;
    needsLEDUpdate = true;

    if (tapoutScrollPos == 0) {
      if (!tapoutAudioTriggeredThisCycle) {
        rapidPatternActive = true;
        patternStep = 0;
        lastPatternTick = currentMillis;
        tapoutAudioTriggeredThisCycle = true; // Guard it so it doesn't continuously spam at pos 0
      }
    } else {
      tapoutAudioTriggeredThisCycle = false;
    }
    
    tapoutScrollPos++;
    if (tapoutScrollPos > (textLength - 5)) {
      tapoutScrollPos = 0;
    } 
  }
}

void triggerBeep(uint32_t durationMs) {
    if (!audioEnabled) return;

    beepEndTime = millis() + durationMs;
    if (beepActive) return; 

    beepActive = true;

    if (audioOutputSelect == 0) {
        tone(BUZZ_PIN, 2000); 
    } else {
        digitalWrite(RELAY_PIN, HIGH); 
    }
}

void checkAudioTimeout() {
  unsigned long currentMillis = millis();

  if (rapidPatternActive) {
    // Dynamically choose interval: 60ms for Buzzer (0), 120ms for Relay (1)
    unsigned long dynamicInterval = (audioOutputSelect == 0) ? 60 : 120;

    if (currentMillis - lastPatternTick >= dynamicInterval) {
      lastPatternTick = currentMillis;
      patternStep++;

      if (patternStep < 8) {
        bool shouldBeep = (patternStep % 2 == 0);
        if (shouldBeep && audioEnabled) {
            if (audioOutputSelect == 0) tone(BUZZ_PIN, 2000);
            else digitalWrite(RELAY_PIN, HIGH);
        } else {
            noTone(BUZZ_PIN);
            digitalWrite(RELAY_PIN, LOW);
        }
      } 
      else {
        noTone(BUZZ_PIN);
        digitalWrite(RELAY_PIN, LOW);
        rapidPatternActive = false;
      }
    }
    return; 
  }

  if (!beepActive) return;

  if (currentMillis >= beepEndTime) {
      noTone(BUZZ_PIN);
      digitalWrite(RELAY_PIN, LOW);
      beepActive = false;
  }
}
