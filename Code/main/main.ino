#include <WiFi.h>
#include <WebServer.h>
#include "WebSocketsServer.h"
#include "browser.h"
#include "config.h"
#include "display.h"
#include "timerLogic.h"

Preferences preferences;

// Instantiate the objects here
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

const char* apSSID = "TimerSetup";
const char* apPassword = "12345678";

int countdown_time = 120; // Default to 2 minutes (120 seconds)
int current_time = countdown_time;
bool timeSelState = false;
bool blueReady = false;
bool redReady = false;
bool readyRequired = false; // default to requiring ready-up

bool border_toggle = 1;

// ESP-NOW Globals
bool pairingMode = false;
uint8_t redMAC[6], blueMAC[6], judgeMAC[6];
bool redPaired = false, bluePaired = false, judgePaired = false;
struct_message incoming;

unsigned long lastCountdownTime = 0;

int countdown = 3; //number of seconds until match start

int scrollIndex = 0;
unsigned long lastScrollTime = 0;
volatile bool needsLEDUpdate = false;

unsigned long startAttemptTime = 0;

int currentState = CONNECTING;

// main.ino Globals
CRGB digitColor = CRGB::Red; // Default
uint8_t systemBrightness = 255;

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  memcpy(&incoming, data, sizeof(incoming));
  uint8_t* mac = info->src_addr;

  static unsigned long lastPacketTime = 0;
  if (millis() - lastPacketTime < 300) return; 
    lastPacketTime = millis();

  memcpy(&incoming, data, sizeof(incoming));

  Serial.printf("[ESP-NOW] Role: %s | Button: %d\n", incoming.deviceType, incoming.buttonID);

  if (pairingMode) { 
      // Logic to save MACs to Preferences (similar to timer_wifi_logic.ino)
      saveRole(mac, String(incoming.deviceType));
      pairingMode = false; 
      return; 
  }

  // Trigger actions based on incoming data
  if (strcmp(incoming.deviceType, "RedReady") == 0) {
      setTeamReady("Red");
  } 
  else if (strcmp(incoming.deviceType, "BlueReady") == 0) {
      setTeamReady("Blue");
  }
  else if (strcmp(incoming.deviceType, "Judge") == 0) {
    switch(incoming.buttonID) {
        case 1: processCommand("start");  break;
        case 2: processCommand("pause");  break;
        case 3: processCommand("reset");  break;
        case 4: processCommand("switch"); break;
    }
  }
}

void saveRole(const uint8_t *mac, String role) {
    // Ensure 'preferences' is the global Preferences object used in your setup()
    preferences.begin("bot-timer", false); 
    
    if (role == "RedReady") {
        memcpy(redMAC, mac, 6);
        preferences.putBytes("redMAC", mac, 6);
        redPaired = true;
    } else if (role == "BlueReady") {
        memcpy(blueMAC, mac, 6);
        preferences.putBytes("blueMAC", mac, 6);
        bluePaired = true;
    } else if (role == "Judge") {
        memcpy(judgeMAC, mac, 6);
        preferences.putBytes("judgeMAC", mac, 6);
        judgePaired = true;
    }
    
    preferences.end();
    Serial.println("[BIND] " + role + " paired successfully.");
}

void initNetwork() {
    connectWiFi();

    // 1. Static Web Page
    server.on("/", handleRoot);

    // 2. Control & Configuration Routes
    server.on("/control", handleControl);
    server.on("/settime", handleSetTime);
    server.on("/setwifi", handleSetWiFi);
    
    // 3. Remote Pairing Logic
    server.on("/pair", []() { 
        pairingMode = true; 
        Serial.println("[SYSTEM] Pairing Mode Active");
        server.send(200, "text/plain", "Pairing Mode Active"); 
    });

    server.on("/clear_remotes", []() {
        clearRemotes();
        server.send(200, "text/plain", "Remotes Wiped");
    });

    server.on("/status", []() {
      String json = "{";
      json += "\"pairing\":" + String(pairingMode ? "true" : "false") + ",";
      json += "\"readyRequired\":" + String(readyRequired ? "true" : "false") + ",";
      
      // Explicitly send every possible state
      json += "\"state\":\"";
      if (currentState == CLOCK_MODE) json += "CLOCK_MODE";
      else if (currentState == RUNNING) json += "RUNNING";
      else if (currentState == PAUSED) json += "PAUSED";
      else if (currentState == PRE_COUNTDOWN_LOOP) json += "PRE_COUNTDOWN_LOOP";
      else if (currentState == FINISHED) json += "FINISHED";
      else json += "IDLE";
      json += "\",";

      json += "\"red\":" + String(redPaired ? "true" : "false") + ",";
      json += "\"blue\":" + String(bluePaired ? "true" : "false") + ",";
      json += "\"judge\":" + String(judgePaired ? "true" : "false") + ",";
      
      int minutes = current_time / 60;
      int seconds = current_time % 60;
      char timeBuf[6];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", minutes, seconds);
      json += "\"currentTime\":\"" + String(timeBuf) + "\"";
      json += "}"; 
      
      server.send(200, "application/json", json);
    });


    server.on("/setcolor", []() {
      if (currentState == RUNNING || currentState == PAUSED || currentState == PRE_COUNTDOWN_LOOP) {
          server.send(403, "text/plain", "Locked during match");
          return;
      }

      String hexStr = server.arg("hex");
      // Convert the hex string from the browser (e.g., "ff0000") to a number
      uint32_t hex = strtol(hexStr.c_str(), NULL, 16);
      digitColor = CRGB(hex);
      
      // Save to Flash Memory
      preferences.begin("settings", false);
      preferences.putUInt("digitColor", hex);
      preferences.end();
      
      updateLEDs(); // Redraw digits with the new color
      server.send(200, "text/plain", "Color Saved");
  });

    server.on("/setbrightness", []() {
      if (currentState == RUNNING || currentState == PAUSED || currentState == PRE_COUNTDOWN_LOOP) {
          server.send(403, "text/plain", "Locked during match");
          return;
      }
      int requestedVal = server.arg("val").toInt();
      systemBrightness = constrain(requestedVal, 10, 230);

      preferences.begin("settings", false);
      preferences.putUChar("brightness", systemBrightness);
      preferences.end();

      FastLED.setBrightness(systemBrightness);
      needsLEDUpdate = true;

      server.send(200, "text/plain", "Brightness Set");
    });

    server.on("/synctime", []() {
      // Safety check: Only allow clock mode if we are IDLE or already in CLOCK_MODE
      if (currentState != IDLE && currentState != CLOCK_MODE) {
          server.send(403, "text/plain", "Timer is active!");
          return;
      }

      int h = server.arg("h").toInt();
      int m = server.arg("m").toInt();
      int s = server.arg("s").toInt();

      struct tm tm;
      tm.tm_hour = h;
      tm.tm_min = m;
      tm.tm_sec = s;
      tm.tm_year = 2026 - 1900; 
      tm.tm_mon = 4;            
      tm.tm_mday = 10;          

      time_t t = mktime(&tm);
      struct timeval now = { .tv_sec = t };
      settimeofday(&now, NULL); //
      
      currentState = CLOCK_MODE;
      server.send(200, "text/plain", "Clock Seeded");
    });

    // 5. Start Communication Services
    webSocket.onEvent(onWebSocketEvent);
    webSocket.begin();
    server.begin();
    Serial.println("[SYSTEM] HTTP Server and WebSocket Started");
}

void startAPMode() {
    Serial.println("Starting Access Point mode...");
    WiFi.softAP(apSSID, apPassword);
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
}

void handleRoot() {
  server.send(200, "text/html", html);
}

void handleControl() {
    String cmd = server.arg("cmd");
    
    // Handle the special case where we need an extra argument
    if (cmd == "readytoggle") {
        String state = server.arg("state");
        readyRequired = (state == "on");
        preferences.begin("settings", false);
        preferences.putBool("readyRequired", readyRequired);
        preferences.end();

        if(currentState != RUNNING)
          setBorder();//only reflect when the timer is paused/not running
    } else {
        // Everything else uses the shared logic
        processCommand(cmd);
    }

    server.send(200, "text/plain", "OK");
}

// INSERT NEW FUNCTION HERE:
void handleSetTime() {

  if (currentState == RUNNING || currentState == PRE_COUNTDOWN_LOOP) {
        server.send(403, "text/plain", "Cannot set time while match is active!");
        return;
    }

    int m = server.arg("m").toInt();
    int s = server.arg("s").toInt();
    
    current_time = (m * 60) + s;
    currentState = IDLE;
    
    updateClient();
    updateLEDs();
    
    server.send(200, "text/plain", "Time Updated");
}

void updateClient() {
  int minutes = current_time / 60;
  int seconds = current_time % 60;
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
  webSocket.broadcastTXT(buffer);
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  // Nothing to do here for now
}

void handleSetWiFi() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    
    if (ssid.length() > 0 && pass.length() > 0) {
        preferences.begin("wifi", false);
        preferences.putString("ssid", ssid);
        preferences.putString("pass", pass);
        preferences.end();
        
        server.send(200, "text/html", "<h1>WiFi Credentials Saved! Rebooting...</h1>");
        delay(3000);
        ESP.restart();
    } else {
        server.send(200, "text/html", "<h1>Invalid Input - Both SSID and Password are required</h1>");
    }
}

void connectWiFi() {
    preferences.begin("wifi", true);
    String ssid = preferences.getString("ssid", "");
    String pass = preferences.getString("pass", "");
    preferences.end();
    
    if (ssid.length() == 0 || ssid == "Disconnected") {
        startAPMode();
        return;
    }
    
    WiFi.begin(ssid.c_str(), pass.c_str());
    startAttemptTime = millis(); // Make sure this is a global variable
    currentState = CONNECTING;   // Trigger the animation task
}

void checkWiFiConnection() {
    // If connected, set up the server and stop animating
    if (WiFi.status() == WL_CONNECTED) {
        server.on("/", handleRoot);
        server.on("/control", handleControl);
        server.begin();
        webSocket.begin();
        webSocket.onEvent(onWebSocketEvent);
        
        currentState = IDLE;
        setBorder();
        needsLEDUpdate = true;
        updateClient();
    } 
    // If it takes longer than 30 seconds, fail over to AP
    else if (millis() - startAttemptTime > 30000) {
        startAPMode();

        currentState = IDLE;
        setBorder();
        needsLEDUpdate = true;
        updateClient();
    }
}



void clearRemotes() {
    Serial.println("[SYSTEM] Wiping remote bindings...");
    
    // Clear Flash Storage
    preferences.begin("bot-timer", false); 
    preferences.remove("redMAC"); 
    preferences.remove("blueMAC"); 
    preferences.remove("judgeMAC");
    preferences.end();
    
    // Reset RAM Flags
    redPaired = false;
    bluePaired = false;
    judgePaired = false;
    
    // Reset the internal buffers to be safe
    memset(redMAC, 0, 6);
    memset(blueMAC, 0, 6);
    memset(judgeMAC, 0, 6);
    
    Serial.println("[SYSTEM] All remotes wiped.");
}

void loadSavedSettings() {
    preferences.begin("bot-timer", true); // Open read-only
    
    // Check if the keys exist and have data (returning 6 bytes means it was stored)
    redPaired   = (preferences.getBytes("redMAC", redMAC, 6) == 6);
    bluePaired  = (preferences.getBytes("blueMAC", blueMAC, 6) == 6);
    judgePaired = (preferences.getBytes("judgeMAC", judgeMAC, 6) == 6);
    
    preferences.end();
    
    Serial.println("[SYSTEM] Pairing status recovered:");
    Serial.printf("Red: %s, Blue: %s, Judge: %s\n", 
                  redPaired ? "PAIRED" : "OPEN", 
                  bluePaired ? "PAIRED" : "OPEN", 
                  judgePaired ? "PAIRED" : "OPEN");

    preferences.begin("settings", true);  // read-only
    readyRequired = preferences.getBool("readyRequired", false); 
    timeSelState = preferences.getBool("timeSelState", false); 

    systemBrightness = preferences.getUChar("brightness", 230);
    systemBrightness = constrain(systemBrightness, 10, 230); // Guard against old saved data
    FastLED.setBrightness(systemBrightness);

    uint32_t savedColor = preferences.getUInt("digitColor", 0xFF0000);
    digitColor = CRGB(savedColor);

    preferences.end();

    countdown_time = timeSelState ? 180 : 120;
    current_time = countdown_time;
}

void setup() {
  Serial.begin(115200);

  // Load saved Settings
  loadSavedSettings();

  // Initialize LED Display
  initDisplay();

  //Initialize Network
  initNetwork();
  
  updateLEDs();//update the text LEDs
  setBorder();//start the border on
  FastLED.show();

  // Set up the buttons and switch pins
  pinMode(RESET_BTN, INPUT_PULLUP);
  pinMode(PAUSE_BTN, INPUT_PULLUP);
  pinMode(START_BTN, INPUT_PULLUP);
  pinMode(TIME_SEL_SW, INPUT_PULLUP);
  pinMode(BLUE_BTN, INPUT_PULLUP);
  pinMode(RED_BTN, INPUT_PULLUP);

  if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    
  esp_now_register_recv_cb(OnDataRecv);
    
  xTaskCreate(
    [](void*) {
        TickType_t lastWakeTime = xTaskGetTickCount();
        while (true) {

          switch(currentState)
          {
            case CONNECTING:
              handleConnectingAnimation();
              checkWiFiConnection();
              
            break;

            case PRE_COUNTDOWN_INIT:
              startPreCountdown();
            break;

            case PRE_COUNTDOWN_LOOP:
              handlePreCountdownAnimation();
            break;

            case RUNNING:
              updateTimer();
            break;

            case PAUSED:
              handlePausedBlink();
            break;

            case FINISHED:
            break;

            case CLOCK_MODE:
              handleClockMode();
            break;
          
          }

          // LED Refresh Consumer
          if (needsLEDUpdate) {
              FastLED.show();
              needsLEDUpdate = false;
          }
          vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1));
        }
    },"TimerTask",4096, nullptr, 1,nullptr
  );

  // 2. Button Polling Task
  xTaskCreate(
    [](void*) {
        while (true) {
            checkButtons();
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    },"ButtonCheckTask",2048,nullptr,1,nullptr
  );
}


void loop() {
  server.handleClient();
  webSocket.loop();
}

