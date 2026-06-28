#include <ESP8266WiFi.h>
#include <espnow.h>

// --- CONFIGURATION ---
const char* DEV_TYPE = "RedReady";//BlueReady
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct struct_message {
    char deviceType[15];
    int buttonID;
} struct_message;

struct_message myData;

void setup() {
    // Start serial quickly for debugging
    Serial.begin(115200);
    Serial.println("\n[!] Wake detected. Initializing Shotgun Blast...");
    
    // 1. WiFi Prep
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(); 

    if (esp_now_init() != 0) {
        Serial.println("ESP-NOW Init Failed");
        ESP.deepSleep(0); // Sleep and try again next press
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
    
    // Add the peer initially on Channel 1 (we will update this in the loop)
    esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

    // 2. Prepare the data packet
    strcpy(myData.deviceType, DEV_TYPE);
    myData.buttonID = 1;

    for (int ch = 1; ch <= 11; ch++) {
        wifi_set_channel(ch);
        esp_now_set_peer_channel(broadcastAddress, ch);
        
        // Small internal loop for extra insurance on each channel
        for(int extra = 0; extra < 3; extra++) {
            esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
            delay(2); // very short gap
        }
        
        Serial.printf("Blasted Channel %d (3x)\n", ch);
    }

    // 4. Mission Complete
    Serial.println("Sweep complete. Entering Deep Sleep (Infinite).");
    
    // Give the last packet a tiny bit of time to leave the buffer 
    // before we yank the power from the radio.
    delay(50);
    
    // Deep sleep (0) means sleep until the RST pin is pulled LOW
    ESP.deepSleep(0); 
}

void loop() {
    // Unused in Deep Sleep workflow
}