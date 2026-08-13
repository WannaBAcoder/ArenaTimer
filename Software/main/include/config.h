#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <time.h>

// Verbose per-event debug logging (every button press, every command, every
// LoRa packet). Controlled by ENABLE_DEBUG_LOG (see platformio.ini) so it
// can be compiled out for quieter/slightly faster event-day builds without
// touching source. Defaults to on, matching prior always-on behavior.
#ifdef ENABLE_DEBUG_LOG
  #define DEBUG_LOG(...) Serial.printf(__VA_ARGS__)
#else
  #define DEBUG_LOG(...)
#endif

// Pin assignments
#define RESET_BTN 15
#define PAUSE_BTN 14
#define START_BTN 27
#define TIME_SEL_SW 13
#define LED_BUILTIN 2
#define BLUE_BTN 4
#define RED_BTN 16
#define BUZZ_PIN 18
#define RELAY_PIN 21

// RA-08H LoRa module (replaces ESP-NOW), see PCB Files/ESP32_Lora netlist.
// UART2: GPIO23 TX (-> module LPRXD), GPIO22 RX (<- module TXD).
#define RADIO_RST_PIN 25
#define RADIO_UART_TX 23
#define RADIO_UART_RX 22
// Hard limit: the module's AT interface runs on its LPUART, which cannot
// exceed 9600 baud. Must match the RA08H_p2p_bridge firmware.
#define RADIO_BAUD 9600

// LED pin assignments
#define DIGIT_PIN   5
#define BORDER_PIN  17
#define DIGIT_LED_COUNT 202
#define BORDER_LED_COUNT 140
#define LEFT_BORDER 0
#define RIGHT_BORDER 70
#define ORANGE CRGB(255, 50, 0)

// Strip calculation constants
#define HALF_DIGIT (DIGIT_LED_COUNT / 2)
#define HALF_BORDER (BORDER_LED_COUNT / 2)
#define PHYSICAL_STRIP_LEN (HALF_DIGIT + HALF_BORDER)

// Double-sided absolute memory buffer requirement
#define DOUBLE_STRIP_LEN (PHYSICAL_STRIP_LEN * 2)

// Add to Config.h
extern CRGB digitColor;
extern uint8_t systemBrightness;

// Global Objects
extern Preferences preferences;

// Constants
const float scrollInterval = 1000.0 / BORDER_LED_COUNT;
const unsigned long debounceDelay = 200;

// Add these to your "Global Objects" or "Constants" section
extern bool pairingMode;
extern bool redPaired, bluePaired, judgePaired;

// Add these to Config.h
extern int current_time;
extern bool readyRequired;

extern int countdown_time;
extern bool blueReady;
extern bool redReady;

extern bool displayInverted;

extern bool tapoutEnabled; // Track if tapouts are allowed during a match
extern bool tapoutInitiatorIsBlue;

extern bool audioEnabled;
extern bool remoteAudioEnabled;
extern uint8_t audioOutputSelect; // 0 = Buzzer (Tone), 1 = Relay (Digital Pin)

// Non-blocking beep engine function prototypes
void triggerBeep(uint32_t durationMs);
void checkAudioTimeout();

enum timerState {
    CONNECTING,
    IDLE,
    PRE_COUNTDOWN_INIT,
    PRE_COUNTDOWN_LOOP,
    RUNNING,
    PAUSED,
    FINISHED,
    CLOCK_MODE, 
    TAPOUT
};

extern int currentState;

// Function prototypes for functions defined in other modules
void starPreCountdown(); // Defined in TimerLogic
void updateClient();     // Defined in Network
void updateLEDs();       // Defined in Display
void setBorder();        // Defined in Display
void queueCommand(const char* cmd); // Defined in main.cpp

// Defined in loraRemotes.cpp
void loraInit();
void loraPoll();
void loraLoadSavedRemotes();
void clearRemotes();

#endif