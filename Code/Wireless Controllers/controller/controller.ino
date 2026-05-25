#include <ESP8266WiFi.h>
#include <espnow.h>

extern "C" {
  #include "user_interface.h"
}

// --- CONFIGURATION ---
const char* DEV_TYPE = "Judge"; 
const int PINS[] = {14, 12, 13, 5}; 
const int BUTTON_IDS[] = {1, 2, 3, 4}; 
const int NUM_BUTTONS = 4;
const int DEBOUNCE_MS = 50; 

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct struct_message {
    char deviceType[15];
    int buttonID;
} __attribute__((packed)) struct_message;

struct_message myData;

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
    Serial.printf("[!] Heavy Sweep for Command %d...\n", id);

    for (int ch = 1; ch <= 11; ch++) {
        wifi_set_channel(ch);
        esp_now_set_peer_channel(broadcastAddress, ch);
        
        // Instead of 1 packet, send 5 packets on EVERY channel
        // This ensures that when we hit the RIGHT channel, 
        // we stay there long enough for a busy Host to hear us.
        for (int i = 0; i < 5; i++) {
            esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
            delay(1); // Rapid fire
        }
        
        // Small pause to let the radio settle before hopping to the next frequency
        delay(2); 
    }
    Serial.println("[✓] Heavy Sweep complete.");
}

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n--- LOW POWER BATTLE TIMER REMOTE ---");
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); 

    // Explicitly set the channel to 1 (or 6, or 11)
    // This MUST match the receiver's channel.
    int targetChannel = 1; 
    
    // On ESP8266, we use the SDK function to force the channel
    wifi_set_channel(targetChannel);

    if (esp_now_init() != 0) {
        Serial.println("ESP-NOW Init Failed");
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, targetChannel, NULL, 0);

    for(int i = 0; i < NUM_BUTTONS; i++) {
        pinMode(PINS[i], INPUT_PULLUP);
        // Enable wake-up on LOW level for each button
        gpio_pin_wakeup_enable(GPIO_ID_PIN(PINS[i]), GPIO_PIN_INTR_LOLEVEL);
    }

    // Set sleep type to Light Sleep
    wifi_set_sleep_type(LIGHT_SLEEP_T);
    
    strcpy(myData.deviceType, DEV_TYPE);
    Serial.println("System Ready. Entering Light Sleep mode between presses.");
}

void loop() {
    bool activityDetected = false;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (digitalRead(PINS[i]) == LOW) {
            activityDetected = true;
            Serial.printf("Wake-up triggered by Pin %d\n", PINS[i]);
            
            blastMessage(BUTTON_IDS[i]);
            
            // Debounce/Hold: Wait for user to let go
            while(digitalRead(PINS[i]) == LOW) {
                delay(10); 
            }
            Serial.println("Button released. Preparing for sleep...");
        }
    }

    // If you see this print quickly after a press, the wake-up worked.
    // The delay(100) is the "window" where the chip actually sleeps.
    delay(100); 
    if (activityDetected) {
        Serial.println("Loop finished. System idling...");
    }
}