#include <ESP8266WiFi.h>
#include <espnow.h>

// --- CONFIGURATION ---
const char* DEV_TYPE = "RedReady"; 
// Note: We don't need BUTTON_PIN anymore because the Reset button is hardware-level
// ---------------------

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct struct_message {
    char deviceType[15];
    int buttonID;
} struct_message;

struct_message myData;

void setup() {
  Serial.begin(115200);
  
  // Initialize WiFi
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); 

  if (esp_now_init() != 0) {
    return;
  }

  // Use the exact syntax from your working code
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

  // Prepare the data packet
  strcpy(myData.deviceType, DEV_TYPE);
  myData.buttonID = 1;

  Serial.println("[!] Reset Triggered. Blasting packets...");

  // The "Blast" Loop
  for(int i = 0; i < 10; i++) {
    esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
    delay(5); 
  }

  // Brief delay to ensure radio buffer clears
  delay(100);

  // Deep sleep forever. 
  // Power draw drops to ~20uA. 
  // It only wakes up when the RST button is pressed.
  Serial.println("Mission complete. Going to sleep.");
  ESP.deepSleep(0); 
}

void loop() {
  // Unreachable
}