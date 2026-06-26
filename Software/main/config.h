#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <FastLED.h>
#include <Preferences.h>
#include <esp_now.h> // Add this include
#include <time.h>

// Pin assignments
#define RESET_BTN 12
#define PAUSE_BTN 14
#define START_BTN 27
#define TIME_SEL_SW 13
#define LED_BUILTIN 2
#define BLUE_BTN 4
#define RED_BTN 16
#define BUZZ_PIN 18
#define RELAY_PIN 21

// LED pin assignments
#define DIGIT_PIN   5
#define BORDER_PIN  17
#define DIGIT_LED_COUNT 202
#define BORDER_LED_COUNT 140
#define LEFT_BORDER 0
#define RIGHT_BOARDER 70
#define ORANGE CRGB(255, 50, 0)

// Strip calculation constants
#define HALF_DIGIT (DIGIT_LED_COUNT / 2)
#define HALF_BORDER (BORDER_LED_COUNT / 2)
#define PHYSICAL_STRIP_LEN (HALF_DIGIT + HALF_BORDER)

// Add to Config.h
extern CRGB digitColor;
extern uint8_t systemBrightness;

// Global Objects
extern Preferences preferences;

// Constants
const float scrollInterval = 1000.0 / BORDER_LED_COUNT;
const unsigned long debounceDelay = 200;

// Add the struct definition
typedef struct struct_message {
    char deviceType[15];
    int buttonID;
} __attribute__((packed)) struct_message;

// Add these to your "Global Objects" or "Constants" section
extern bool pairingMode;
extern bool redPaired, bluePaired, judgePaired;

// Add these to Config.h
extern int current_time;
extern bool readyRequired;
extern bool preCountdownRunning;

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

extern bool slaveModeEnabled;

typedef struct master_sync_message {
    uint8_t packetType;       // 0xAA = Master Sync Packet
    uint8_t systemState;      // Current timerState enum (RUNNING, PAUSED, IDLE, etc.)
    int current_time;         // Real-time remaining seconds
    uint32_t digitColorHex;   // 24-bit Hex color representation (e.g., 0xFF0000)
    uint8_t brightness;       // System brightness value (10 - 230)
    bool displayInverted;     // Orientation layout state
    bool audioEnabled;        // Global sound toggle
    bool remoteAudioEnabled;  // Remote buzzer allowance filter
    bool tapoutInitiatorIsBlue; // Active team identifier during tapouts
} __attribute__((packed)) master_sync_message;

void broadcastMasterSync();

extern int currentState;

// Function prototypes for functions defined in other modules
void starPreCountdown(); // Defined in TimerLogic
void updateClient();     // Defined in Network
void updateLEDs();       // Defined in Display
void setBorder();        // Defined in Display

#endif