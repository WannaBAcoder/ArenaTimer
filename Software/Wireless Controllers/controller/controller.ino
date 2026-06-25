#include <ESP8266WiFi.h>
#include <espnow.h>

extern "C" {
  #include "user_interface.h"
}

// --- CONFIGURATION ---
const char* DEV_TYPE = "Judge"; 
const int PINS[] = {13, 12, 14, 4, 5}; 
const int BUTTON_IDS[] = {1, 2, 3, 4, 5}; 
const int NUM_BUTTONS = 5;

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct struct_message {
    char deviceType[15];
    int buttonID;
} __attribute__((packed)) struct_message;

struct_message myData;

// Translates numerical button IDs to readable function labels
String getButtonLabel(int id) {
  switch(id) {
    case 1: return "START";
    case 2: return "PAUSE";
    case 3: return "RESET";
    case 4: return "TIME SELECT";
    case 5: return "BUZZER";
    default: return "UNKNOWN";
  }
}

// Blasts packets across all 11 channels
void shotgunBlast(int id, int packetsPerChannel) {
    myData.buttonID = id;
    
    for (int ch = 1; ch <= 11; ch++) {
        wifi_set_channel(ch);
        esp_now_set_peer_channel(broadcastAddress, ch);
        
        for (int i = 0; i < packetsPerChannel; i++) {
            esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
            delay(2); 
        }
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n--- SIMPLIFIED BATTLE TIMER REMOTE ---");
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); 

    if (esp_now_init() != 0) {
        Serial.println("ESP-NOW Init Failed");
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

    for(int i = 0; i < NUM_BUTTONS; i++) {
        pinMode(PINS[i], INPUT_PULLUP);
        gpio_pin_wakeup_enable(GPIO_ID_PIN(PINS[i]), GPIO_PIN_INTR_LOLEVEL);
    }

    wifi_set_sleep_type(LIGHT_SLEEP_T);
    strcpy(myData.deviceType, DEV_TYPE);
    Serial.println("System Ready. Fire-and-forget mode active.");
}

void loop() {
    bool activityDetected = false;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (digitalRead(PINS[i]) == LOW) {
            activityDetected = true;
            
            // Get the readable button string label (e.g., "START", "BUZZER")
            String btnLabel = getButtonLabel(BUTTON_IDS[i]);
            Serial.printf("[!] Triggered Action: %s\n", btnLabel.c_str());
            
            if (PINS[i] == 5) { // BUZZER HOLD (GPIO 5 / D1)
                Serial.println("  -> Continuous Spaced Shotgun Stream active...");
                while (digitalRead(PINS[i]) == LOW) {
                    // Send a fast 1-packet-per-channel sweep across the spectrum (~30ms)
                    shotgunBlast(BUTTON_IDS[i], 1);
                    
                    // 90ms gap lets the spectrum clear and ensures the timer's core loops don't choke
                    delay(90); 
                }
            } else { // REGULAR BUTTON CLICK
                // Send a highly secure 3-packet-per-channel blast to guarantee delivery
                shotgunBlast(BUTTON_IDS[i], 3);
                
                while(digitalRead(PINS[i]) == LOW) {
                    delay(10); // Hold execution until the user lets go of the button
                }
            }
            Serial.printf("[✓] %s released. Returning to low-power idle.\n", btnLabel.c_str());
        }
    }

    delay(100); 
}