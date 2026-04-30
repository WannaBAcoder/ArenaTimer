#include <ESP8266WiFi.h>
#include <espnow.h>

// --- CONFIGURATION ---
const char* DEV_TYPE = "Judge"; 

// Button 1: Start        = D5 (GPIO 14)
// Button 2: Pause        = D6 (GPIO 12)
// Button 3: Reset        = D7 (GPIO 13) 
// Button 4: Time Select  = D1 (GPIO 5) 
const int PINS[] = {14, 12, 13, 5}; 
const int BUTTON_IDS[] = {1, 2, 3, 4}; 
const int NUM_BUTTONS = 4;

const int DEBOUNCE_MS = 50; 
// ---------------------

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct struct_message {
    char deviceType[15];
    int buttonID;
} __attribute__((packed)) struct_message;

struct_message myData;

unsigned long lastPressTime[NUM_BUTTONS] = {0, 0, 0, 0};
bool lastButtonState[NUM_BUTTONS] = {HIGH, HIGH, HIGH, HIGH};

String getButtonLabel(int id) {
  switch(id) {
    case 1: return "START";
    case 2: return "PAUSE";
    case 3: return "RESET";
    case 4: return "TIME SELECT";
    default: return "UNKNOWN";
  }
}

void blastMessage(int id) {
  myData.buttonID = id;
  Serial.printf("[!] Sending Command: %d (%s)\n", id, getButtonLabel(id).c_str());
  
  // The Redundancy Loop
  // Sending 5 times ensures that at least one packet makes it through RF noise.
  for(int i = 0; i < 5; i++) {
    esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
    delay(2); // Short delay to let the radio clear the buffer
  }
}

void setup() {
  Serial.begin(115200);
  delay(500); 
  Serial.println("\n--- BATTLE TIMER: 4-BUTTON JUDGE REMOTE ---");

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); 

  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

  for(int i = 0; i < NUM_BUTTONS; i++) {
    pinMode(PINS[i], INPUT_PULLUP);
  }

  strcpy(myData.deviceType, DEV_TYPE);
  Serial.println("System Ready. All buttons Active-Low (GND).");
}

void loop() {
  for (int i = 0; i < NUM_BUTTONS; i++) {
    bool currentState = digitalRead(PINS[i]);

    if (currentState != lastButtonState[i]) {
      if ((millis() - lastPressTime[i]) > DEBOUNCE_MS) {
        if (currentState == LOW) {
          blastMessage(BUTTON_IDS[i]);
        }
        lastPressTime[i] = millis();
      }
    }
    lastButtonState[i] = currentState;
  }
  yield(); 
}