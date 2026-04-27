#include <ESP8266WiFi.h>
#include <espnow.h>

// --- CONFIGURATION ---
const char* DEV_TYPE = "Driver"; // Change to "Judge" for the 3-button unit
const int BUTTON_PIN = 4;        // GPIO 4 is D2 on the Wemos D1 Mini
// ---------------------

// Broadcast address works for the initial "Pairing" phase
uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

typedef struct struct_message {
    char deviceType[15];
    int buttonID;
} struct_message;

struct_message myData;

// Callback when data is sent (helps for debugging)
void OnDataSent(uint8_t *mac_addr, uint8_t sendStatus) {
  Serial.print("Last Packet Delivery Status: ");
  if (sendStatus == 0) Serial.println("Delivery Success");
  else Serial.println("Delivery Fail");
}

void setup() {
  Serial.begin(115200);
  delay(1000); 

  Serial.println("\n--- BATTLEBOTS REMOTE BOOT ---");
  Serial.printf("Device Type: %s\n", DEV_TYPE);
  Serial.printf("Monitoring Pin: D2 (GPIO %d)\n", BUTTON_PIN);

  // Initialize WiFi in Station Mode
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(); // We don't want to connect to a router

  // Init ESP-NOW
  if (esp_now_init() != 0) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  // Set up the role and add the broadcast peer
  esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
  esp_now_register_send_cb(OnDataSent);
  esp_now_add_peer(broadcastAddress, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  strcpy(myData.deviceType, DEV_TYPE);
  Serial.println("System Ready. Short D2 to GND to trigger.");
}

void loop() {
  // Check if button is pressed (LOW)
  if (digitalRead(BUTTON_PIN) == LOW) {
    myData.buttonID = 1; // For single button test, ID is always 1

    Serial.println("[!] Button Triggered. Blasting packets...");

    // Send the message 10 times (redundancy for noisy environments)
    for(int i = 0; i < 10; i++) {
      esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
      delay(5); // Small gap between bursts
    }

    // Software debounce: wait for button release + a bit of padding
    while(digitalRead(BUTTON_PIN) == LOW) { delay(10); } 
    delay(200); 
    Serial.println("Ready for next press.");
  }
}