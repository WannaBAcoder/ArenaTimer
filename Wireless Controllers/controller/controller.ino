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
    Serial.printf("[!] Sending Command %d across all channels...\n", id);

    // We cycle through 1 to 11. 
    // (Channels 12-14 are restricted in some regions, so 1-11 is safest)
    for (int ch = 1; ch <= 11; ch++) {
        
        // 1. Force the radio to the new channel
        wifi_set_channel(ch);
        
        // 2. Update the peer info to this channel
        // broadcastAddress is the 0xFF... address from your config
        esp_now_set_peer_channel(broadcastAddress, ch);
        
        // 3. Send the packet
        // We only send ONCE per channel because the channel hop 
        // acts as its own form of redundancy.
        esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
        
        // 4. Critical: Short delay to allow the radio to finish the TX 
        // and for the PLL to stabilize on the next frequency.
        delay(4); 
    }
    
    Serial.println("[✓] Sweep complete.");
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