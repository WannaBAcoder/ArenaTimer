#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <espnow.h>
#include "browser.h"

// Replicate the exact struct matching your ESP32 configuration
typedef struct master_sync_message {
    uint8_t packetType;       // 0xAA for validation
    uint8_t systemState;      // Current timerState enum
    int current_time;         // Active count matching Master
    uint32_t digitColorHex;   // 24-bit hex color values
    uint8_t brightness;       // Layout brightness
    bool displayInverted;     // Orientation layout state
    bool audioEnabled;        // Global sound toggle
    bool remoteAudioEnabled;  // Remote buzzer filter
    bool tapoutInitiatorIsBlue; // Active team identifier during tapouts
} __attribute__((packed)) master_sync_message;

ESP8266WebServer server(80);

// Global mock state variables to simulate the Master environment
int test_current_time = 120;
uint8_t test_state = 1; // Start in IDLE (matching your enum: CONNECTING=0, IDLE=1, etc.)
uint32_t test_color = 0xFF0000;
uint8_t test_brightness = 127;
bool test_inverted = false;
bool test_audio = true;
bool test_remote_audio = true;
bool test_tapout_blue = false;

// Universal Broadcast Address
uint8_t broadcastMAC[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

void sendSyncPacket() {
    master_sync_message msg;
    msg.packetType = 0xAA;
    msg.systemState = test_state;
    msg.current_time = test_current_time;
    msg.brightness = test_brightness;
    msg.displayInverted = test_inverted;
    msg.audioEnabled = test_audio;
    msg.remoteAudioEnabled = test_remote_audio;
    msg.tapoutInitiatorIsBlue = test_tapout_blue;
    msg.digitColorHex = test_color;

    // Send via ESP8266 esp_now_send mechanism
    esp_now_send(broadcastMAC, (uint8_t *)&msg, sizeof(msg));
    Serial.printf("Broadcasted Sync: Time=%d, State=%d, Color=0x%06X\n", test_current_time, test_state, test_color);
}

void handleRoot() {
    server.send(200, "text/html", html);
}

void handleStatus() {
    // Generate the JSON format the browser script expects to parse on intervals
    String json = "{";
    json += "\"slaveModeEnabled\":false,";
    json += "\"pairing\":false,";
    json += "\"readyRequired\":false,";
    json += "\"tapoutEnabled\":true,";
    json += "\"audioEnabled\":" + String(test_audio ? "true" : "false") + ",";
    json += "\"remoteAudioEnabled\":" + String(test_remote_audio ? "true" : "false") + ",";
    json += "\"audioOutput\":0,";
    
    json += "\"state\":\"";
    if (test_state == 4) json += "RUNNING";      // Enum tracking mapping offsets
    else if (test_state == 5) json += "PAUSED";
    else if (test_state == 8) json += "TAPOUT";
    else json += "IDLE";
    json += "\",";
    
    json += "\"tapoutBlue\":" + String(test_tapout_blue ? "true" : "false") + ",";
    json += "\"red\":true,\"blue\":true,\"judge\":true,"; // Force indicators to show active connections
    json += "\"brightness\":" + String(test_brightness) + ",";
    json += "\"displayInverted\":" + String(test_inverted ? "true" : "false") + ",";
    
    char hexColor[7];
    snprintf(hexColor, sizeof(hexColor), "%06X", test_color);
    json += "\"digitColor\":\"" + String(hexColor) + "\",";
    
    int minutes = test_current_time / 60;
    int seconds = test_current_time % 60;
    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", minutes, seconds);
    json += "\"currentTime\":\"" + String(timeBuf) + "\"";
    json += "}";
    
    server.send(200, "application/json", json);
}

void handleControl() {
    String cmd = server.arg("cmd");
    Serial.println("Control Command received: " + cmd);
    
    if (cmd == "start")   test_state = 4; // RUNNING
    else if (cmd == "pause")  test_state = 5; // PAUSED
    else if (cmd == "reset")  { test_state = 1; test_current_time = 120; } // IDLE
    else if (cmd == "switch") { test_current_time = (test_current_time == 120) ? 180 : 120; }
    
    sendSyncPacket();
    server.send(200, "text/plain", "OK");
}

void handleSetTime() {
    int m = server.arg("m").toInt();
    int s = server.arg("s").toInt();
    test_current_time = (m * 60) + s;
    test_state = 1; // Drop to IDLE state
    
    sendSyncPacket();
    server.send(200, "text/plain", "Time Updated");
}

void handleSetColor() {
    String hexStr = server.arg("hex");
    test_color = strtol(hexStr.c_str(), NULL, 16);
    
    sendSyncPacket();
    server.send(200, "text/plain", "Color Saved");
}

void handleSetBrightness() {
    test_brightness = server.arg("val").toInt();
    
    sendSyncPacket();
    server.send(200, "text/plain", "Brightness Set");
}

void handleFlip() {
    test_inverted = !test_inverted;
    
    sendSyncPacket();
    server.send(200, "text/plain", "Flipped");
}

void handleSetAudio() {
    if (server.hasArg("enabled")) test_audio = (server.arg("enabled") == "true");
    if (server.hasArg("remoteEnabled")) test_remote_audio = (server.arg("remoteEnabled") == "true");
    
    sendSyncPacket();
    server.send(200, "text/plain", "Audio Settings Saved");
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- Starting ESP8266 Master Tester ---");

    // Initialize WiFi in Station mode for ESP-NOW and SoftAP for local connectivity
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP("TesterMasterAP", "12345678");
    Serial.print("Connect to AP 'TesterMasterAP' and navigate to: http://");
    Serial.println(WiFi.softAPIP());

    // Initialize ESP-NOW on ESP8266
    if (esp_now_init() != 0) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    // Set role as Controller/Master
    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
    
    // Register the universal broadcast address as a peer
    esp_now_add_peer(broadcastMAC, ESP_NOW_ROLE_SLAVE, 1, NULL, 0);

    // Bind endpoints to mock handlers matching your API signatures
    server.on("/", handleRoot);
    server.on("/status", handleStatus);
    server.on("/control", handleControl);
    server.on("/settime", handleSetTime);
    server.on("/setcolor", handleSetColor);
    server.on("/setbrightness", handleSetBrightness);
    server.on("/flip", handleFlip);
    server.on("/setaudio", handleSetAudio);
    
    server.begin();
    Serial.println("HTTP Server Started.");
}

void loop() {
    server.handleClient();
    
    // Periodically decrement timer if running to simulate clock updates
    static unsigned long lastTick = 0;
    if (test_state == 4 && millis() - lastTick >= 1000) { // 4 = RUNNING
        lastTick = millis();
        if (test_current_time > 0) {
            test_current_time--;
            sendSyncPacket();
        } else {
            test_state = 6; // FINISHED
            sendSyncPacket();
        }
    }
}