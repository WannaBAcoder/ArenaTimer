#include <WiFi.h>
#include <WebServer.h>
#include "WebSocketsServer.h"
#include "browser.h"
#include "config.h"
#include "display.h"

Preferences preferences;

// Instantiate the objects here
WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

const char* apSSID = "TimerSetup";
const char* apPassword = "12345678";
bool setupMode = false;

int countdown_time = 120; // Default to 2 minutes (120 seconds)
int current_time = countdown_time;
bool is_running = false;
bool timeSelState = false;
bool blueReady = false;
bool redReady = false;
bool preCountdownRunning = false;
bool readyRequired = false; // default to requiring ready-up

bool border_toggle = 1;

// ESP-NOW Globals
bool pairingMode = false;
uint8_t redMAC[6], blueMAC[6], judgeMAC[6];
bool redPaired = false, bluePaired = false, judgePaired = false;
struct_message incoming;

// Debounce variables for each button
unsigned long lastDebounceTimeStart = 0;
unsigned long lastDebounceTimePause = 0;
unsigned long lastDebounceTimeReset = 0;
unsigned long lastDebounceTimeTimeSel = 0;
unsigned long lastDebounceTimeBlue = 0;
unsigned long lastDebounceTimeRed = 0;

unsigned long lastCountdownTime = 0;

int countdown = 3; //number of seconds until match start

int scrollIndex = 0;
unsigned long lastScrollTime = 0;
volatile bool needsLEDUpdate = false;

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
    
    // Register these routes regardless of mode
    server.on("/", handleRoot);
    server.on("/setwifi", handleSetWiFi);
    
    // Only register control/websocket routes if we are connected
    // (Optional: keep them globally accessible if you want control in AP mode too)
    server.on("/control", handleControl);
    server.on("/pair", []() { 
        pairingMode = true; 
        server.send(200, "text/plain", "Pairing Mode Active"); 
    });
    server.on("/status", []() { /* ... existing logic ... */ });
    server.on("/clear_remotes", []() { /* ... existing logic ... */ });
    
    webSocket.onEvent(onWebSocketEvent);
    webSocket.begin();
    server.begin();
}

void startAPMode() {
    Serial.println("Starting Access Point mode...");
    WiFi.softAP(apSSID, apPassword);
    Serial.print("AP IP Address: ");
    Serial.println(WiFi.softAPIP());
    setupMode = true;
}

void handleRoot() {
  server.send(200, "text/html", html);
}

void processCommand(String cmd) {
    if (cmd == "start") {
        starPreCountdown();
    } 
    else if (cmd == "pause") {
        is_running = false;
    } 
    else if (cmd == "reset" && !is_running) {
        is_running = false;
        blueReady = redReady = false;
        current_time = countdown_time;
        setBorder();
        updateClient();
        updateLEDs();
    } 
    else if (cmd == "switch" && !is_running) {
        countdown_time = (countdown_time == 120) ? 180 : 120;
        current_time = countdown_time;
        updateClient();
        updateLEDs();
    }
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
        setBorder();
    } else {
        // Everything else uses the shared logic
        processCommand(cmd);
    }

    server.send(200, "text/plain", "OK");
}

// INSERT NEW FUNCTION HERE:
void handleSetTime() {
    int m = server.arg("m").toInt();
    int s = server.arg("s").toInt();
    
    current_time = (m * 60) + s;
    is_running = false; 
    
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

void updateTimer() {
  unsigned long currentMillis = millis();
  
  if (preCountdownRunning) {
    if (currentMillis - lastScrollTime >= scrollInterval) {
      lastScrollTime = currentMillis;

      // Fill from 0 to scrollIndex with ORANGE
      for (int i = 0; i < BORDER_LED_COUNT; i++) {
        if (i >= scrollIndex) {
          setBorderLEDs(i, ORANGE);
        } 
        else {
          setBorderLEDs(i, CRGB::Black);
        }
      }

      FastLED.show();

      scrollIndex--;
      if (scrollIndex < 0) {
        scrollIndex = BORDER_LED_COUNT - 1;
        countdown--;

        if (countdown <= 0) {
          // countdown is done, start timer
          countdown = 3;
          preCountdownRunning = false;
          scrollIndex = BORDER_LED_COUNT - 1;
          lastCountdownTime = 0;
          lastScrollTime = 0;
          is_running = true;
          lastCountdownTime = millis();

          if (!is_running && current_time == countdown_time) {
            current_time = countdown_time + 1;
          }

          // Set border LEDs
          for (int i = 0; i < BORDER_LED_COUNT/2; i++) {
            setBorderLEDs(i, CRGB::Blue); // First half blue
          }

          // Set border LEDs
          for (int i = BORDER_LED_COUNT/2; i < BORDER_LED_COUNT ; i++) {
            setBorderLEDs(i, CRGB::Red); // First half red
          }

          FastLED.show(); // show the initialized border LED colors

          updateClient();
          updateLEDs();
          return; // stop here — don't draw the digit again
        }

        // only draw digit if countdown > 0
        setDigit(0, 0, false);
        setDigit(0, 49, false);
        setColon();
        setDigit(countdown, 101, true);
        setDigit(0, 150, true);
        FastLED.show();
      }
    }
  }

  // Main timer counting down
  if (is_running && current_time > 0) {
    // Countdown every 1 second
    if (currentMillis - lastCountdownTime >= 1000) {
      lastCountdownTime = currentMillis;
      current_time--;
      
      updateClient();  // Reset to 2:00 (or 3:00)
      updateLEDs();
    }
  }

  else {
    is_running = false;
  }
}

void setTeamReady(String team) {
    if (team == "Blue") {
        blueReady = true;
        for (int i = 0; i < BORDER_LED_COUNT/2; i++) {
            setBorderLEDs(i, CRGB::Blue);
        }
    } else if (team == "Red") {
        redReady = true;
        for (int i = BORDER_LED_COUNT/2; i < BORDER_LED_COUNT; i++) {
            setBorderLEDs(i, CRGB::Red);
        }
    }
    
    needsLEDUpdate = true;
}

void checkButtons() {
  unsigned long currentMillis = millis();

  // Start button
  if (digitalRead(START_BTN) == LOW && currentMillis - lastDebounceTimeStart > debounceDelay) {

    starPreCountdown();
    lastDebounceTimeStart = currentMillis;  // Update debounce time
  }

  // Pause button
  if (digitalRead(PAUSE_BTN) == LOW && currentMillis - lastDebounceTimePause > debounceDelay) {
    is_running = false;
    lastDebounceTimePause = currentMillis;
  }

  if(readyRequired) {
    // Blue Ready button
    if (digitalRead(BLUE_BTN) == LOW && (millis() - lastDebounceTimeBlue > debounceDelay)) {
        setTeamReady("Blue");
        lastDebounceTimeBlue = millis();
    }

    // Red Ready button
    if (digitalRead(RED_BTN) == LOW && (millis() - lastDebounceTimeRed > debounceDelay)) {
        setTeamReady("Red");
        lastDebounceTimeRed = millis();
    }
  }

  // Reset button
  if (digitalRead(RESET_BTN) == LOW && (currentMillis - lastDebounceTimeReset > debounceDelay) 
                  && !is_running) {
    is_running = false;
    current_time = countdown_time;
    blueReady = false;
    redReady = false;
    setBorder();
    updateClient();
    updateLEDs();
    lastDebounceTimeReset = currentMillis;
  }

  // Time select switch
  if (digitalRead(TIME_SEL_SW) == LOW && (currentMillis - lastDebounceTimeTimeSel > debounceDelay)  
                  && !is_running) {
    timeSelState = !timeSelState;
    countdown_time = timeSelState ? 180 : 120;
    current_time = countdown_time;

    preferences.begin("settings", false);  // read-write
    preferences.putBool("timeSelState", timeSelState);
    preferences.end();

    updateClient();
    updateLEDs();
    lastDebounceTimeTimeSel = currentMillis;
  }
}

void starPreCountdown() {
  if(!is_running && (!readyRequired || (blueReady && redReady))){
      countdown = 3;
      scrollIndex = BORDER_LED_COUNT - 1;
      preCountdownRunning = true; // Initiate the pre-countdown
      // Use ternary operation to set border LEDs to orange or black
      for (int i = 0; i < BORDER_LED_COUNT; i++) {
        setBorderLEDs(i, ORANGE);
      }

      setDigit(0, 0, false);
      setDigit(0, 49, false);
      setColon();
      setDigit(countdown, 101, true);
      setDigit(0, 150, true);

      FastLED.show();
    }
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  // Nothing to do here for now
}

void handleSetWiFi() {
    String ssid = server.arg("ssid");
    String pass = server.arg("pass");
    
    if (ssid.length() > 0 && pass.length() > 0) {
        ssid = ssid;
        preferences.begin("wifi", false);
        preferences.putString("ssid", ssid);
        preferences.putString("pass", pass);
        preferences.end();
        
        server.send(200, "text/html", "<h1>WiFi Credentials Saved! Rebooting...</h1>");
        delay(3000);
        ESP.restart();
    } else {
        server.send(200, "text/html", "<h1>Invalid Input</h1>");
    }
}

void connectWiFi() {
    preferences.begin("wifi", true);
    String ssid = preferences.getString("ssid", "Disconnected");
    String pass = preferences.getString("pass", "");
    preferences.end();
    
    if (ssid.length() == 0) {
        Serial.println("No WiFi credentials stored, starting AP mode...");
        startAPMode();
        return;
    }
    
    Serial.print("Connecting to WiFi: ");
    Serial.println(ssid);
    WiFi.begin(ssid.c_str(), pass.c_str());
    
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 30000) {
        Serial.print(".");

        if(border_toggle)
        {
          //turn off LEDs
          for (int i = 0; i < BORDER_LED_COUNT; i++) {
            setBorderLEDs(i, CRGB::Black);  // Turn off all LEDs
          }
          FastLED.show();  // Update LEDs
        }
        else
          setBorder();
        border_toggle = !border_toggle;

        delay(500);
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("Connected to WiFi!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());

        server.on("/", handleRoot);
        server.on("/control", handleControl);
        server.begin();

        webSocket.begin();
        webSocket.onEvent(onWebSocketEvent);

        setBorder();
    } else {
        Serial.println("Failed to connect, starting AP mode...");
        startAPMode();
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

void loadPairingStatus() {
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
}

void setup() {
  Serial.begin(115200);
  
  loadPairingStatus();

  preferences.begin("settings", true);  // read-only
  readyRequired = preferences.getBool("readyRequired", false); 
  timeSelState = preferences.getBool("timeSelState", false); 
  preferences.end();

  // Initialize LED strips
  initDisplay();
  initNetwork();
  
  countdown_time = timeSelState ? 180 : 120;
  current_time = countdown_time;
  updateLEDs();//update the text LEDs
  setBorder();//start the border on

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
    
    // Add endpoint for pairing trigger
    server.on("/pair", []() { 
        pairingMode = true; 
        server.send(200, "text/plain", "Pairing Mode Active"); 
    });

    // INSERT NEW ROUTE HERE:
    server.on("/settime", handleSetTime);

    server.on("/status", []() {
        String json = "{";
        json += "\"pairing\":" + String(pairingMode ? "true" : "false") + ",";
        json += "\"red\":" + String(redPaired ? "true" : "false") + ",";
        json += "\"blue\":" + String(bluePaired ? "true" : "false") + ",";
        json += "\"judge\":" + String(judgePaired ? "true" : "false") + ",";
        json += "\"readyRequired\":" + String(readyRequired ? "true" : "false") + ",";
        json += "}";
        server.send(200, "application/json", json);
    });

  server.on("/clear_remotes", []() {
    clearRemotes();
    server.send(200, "text/plain", "Remotes Wiped");
  });

  // 1. Timer & LED Task
  xTaskCreate(
      [](void*) {
          TickType_t lastWakeTime = xTaskGetTickCount();
          while (true) {
              // Timer Logic
              if (is_running || preCountdownRunning) {
                  updateTimer();
              }
              
              // LED Refresh Consumer
              if (needsLEDUpdate) {
                  FastLED.show();
                  needsLEDUpdate = false;
              }
          
              vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1));
          }
      },
      "TimerTask",
      4096,
      nullptr,
      1,
      nullptr
  );

  // 2. Button Polling Task
  xTaskCreate(
      [](void*) {
          while (true) {
              checkButtons();
              vTaskDelay(pdMS_TO_TICKS(50));
          }
      },
      "ButtonCheckTask",
      2048,
      nullptr,
      1,
      nullptr
  );
}


void loop() {
  server.handleClient();
  webSocket.loop();
}

