#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <RadioLib.h>

#define TX_POWER 22 // Full blast +22 dBm

// --- PIN DEFINITIONS ---
#define PIN_NSS     15  // D8
#define PIN_DIO1    16  // D0 (GPIO 16)
#define PIN_BUSY     4  // D2
#define PIN_RFSW_V2  5  // D1
#define PIN_ROLE_A0 A0  // Analog pin for Red/Blue selection

// Module instance: CS=D8, DIO1=D0, RESET=NC, BUSY=D2
LLCC68 radio = new Module(PIN_NSS, PIN_DIO1, RADIOLIB_NC, PIN_BUSY);

// --- BINARY PACKET STRUCTURE ---
struct __attribute__((packed)) RemotePacket {
  uint8_t mac[6];       // ESP8266 Hardware MAC for binding
  char deviceType[15];  // "RedReady" or "BlueReady"
  int32_t buttonID;     // Button action ID
};

RemotePacket myData;

void setup() {
  Serial.begin(115200);
  delay(100); 
  Serial.println("\n[!] Wake detected. Initializing LoRa Remote...");

  // 1. Enable RF Switch Path
  pinMode(PIN_RFSW_V2, OUTPUT);
  digitalWrite(PIN_RFSW_V2, HIGH);

  // 2. Read A0 to determine Device Role
  // Shorted to GND (< 500) = BlueReady | Open/Pulled High (>= 500) = RedReady
  int rawADC = analogRead(PIN_ROLE_A0);
  const char* devRole = (rawADC < 500) ? "BlueReady" : "RedReady";

  // 3. Fetch Hardware MAC Address
  WiFi.macAddress(myData.mac);

  // 4. Prepare Payload Data
  memset(myData.deviceType, 0, sizeof(myData.deviceType));
  strncpy(myData.deviceType, devRole, sizeof(myData.deviceType) - 1);
  myData.buttonID = 1;

  Serial.printf("ADC Read: %d -> Selected Role: %s\n", rawADC, devRole);
  Serial.printf("MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                myData.mac[0], myData.mac[1], myData.mac[2],
                myData.mac[3], myData.mac[4], myData.mac[5]);

  // 5. Initialize LoRa Radio @ 915 MHz, +22 dBm
  int state = radio.begin(915.0, 125.0, 9, 7, 0x12, TX_POWER);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("Radio Init Success. Transmitting payload...");

    // Transmit binary struct directly
    state = radio.transmit((uint8_t*)&myData, sizeof(myData));

    if (state == RADIOLIB_ERR_NONE) {
      Serial.println("--> Packet transmitted successfully!");
    } else {
      Serial.printf("--> Transmission failed, code: %d\n", state);
    }
  } else {
    Serial.printf("Radio init failed, code: %d\n", state);
  }

  // 6. Deep Sleep Sequence
  Serial.println("Entering Deep Sleep (0)...");
  radio.sleep(false);               // Cold sleep LLCC68 (~160 nA)
  digitalWrite(PIN_RFSW_V2, LOW);   // Disable RF switch power path
  
  Serial.flush();
  ESP.deepSleep(0);                 // Deep sleep until reset button press
}

void loop() {
  // Unused in wake-on-reset setup
}