#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
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

bool displayInverted = false;

bool tapoutEnabled = true;
bool tapoutInitiatorIsBlue = true;

bool audioEnabled = true;
bool remoteAudioEnabled = true;
uint8_t audioOutputSelect = 0; // 0 = Buzzer, 1 = Relay
uint32_t beepEndTime = 0;
bool beepActive = false;

// main.ino Globals
CRGB digitColor = CRGB::Red; // Default
uint8_t systemBrightness = 127;

bool slaveModeEnabled = false;

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  if (slaveModeEnabled) {
      if (len == sizeof(master_sync_message) && data[0] == 0xAA) {
          master_sync_message msg;
          memcpy(&msg, data, sizeof(msg));

          // Force Real-time Variables instantly
          current_time = msg.current_time;
          currentState = msg.systemState;
          tapoutInitiatorIsBlue = msg.tapoutInitiatorIsBlue;

          // Check and update cosmetic configurations
          if (systemBrightness != msg.brightness) {
              systemBrightness = msg.brightness;
              FastLED.setBrightness(systemBrightness);
          }
          if (digitColor != CRGB(msg.digitColorHex)) {
              digitColor = CRGB(msg.digitColorHex);
              updateLEDs();
          }
          
          displayInverted = msg.displayInverted;
          audioEnabled = msg.audioEnabled;
          remoteAudioEnabled = msg.remoteAudioEnabled;

          needsLEDUpdate = true;
      }
      return; // Absolute lockout: Salves ignore regular remote packets completely
  }
 
  memcpy(&incoming, data, sizeof(incoming)); //
  uint8_t* mac = info->src_addr; //

  // If it's a buzzer hold packet, we BYPASS the 300ms cooldown so we track the stream.
  static unsigned long lastPacketTime = 0;
  if (incoming.buttonID != 5) { 
      if (millis() - lastPacketTime < 300) return;
      lastPacketTime = millis();
      Serial.printf("[ESP-NOW] Role: %s | Button: %d\n", incoming.deviceType, incoming.buttonID);
  }


  if (pairingMode) { //
      saveRole(mac, String(incoming.deviceType)); //
      pairingMode = false; //
      return; //
  }

  // --- WIRELESS REMOTE LOGIC ---
  if (strcmp(incoming.deviceType, "RedReady") == 0) { //
      if (currentState == RUNNING) { //
          processCommand("tapoutRed");
      } else {
          setTeamReady("Red"); //
      }
  } 
  else if (strcmp(incoming.deviceType, "BlueReady") == 0) { //
      if (currentState == RUNNING) { //
          processCommand("tapoutBlue");
      } else {
          setTeamReady("Blue"); //
      }
  }
  else if (strcmp(incoming.deviceType, "Judge") == 0) { //
    switch(incoming.buttonID) { //
        case 1: processCommand("start");   break; 
        case 2: processCommand("pause");   break; 
        case 3: processCommand("reset");   break; 
        case 4: processCommand("switch");  break; 
        case 5: 
            if (audioEnabled && remoteAudioEnabled) {
                triggerBeep(250); 
            }
            break;
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

        // Update your /status endpoint to include tapoutEnabled
    server.on("/status", []() {
      String json = "{";
      json += "\"pairing\":" + String(pairingMode ? "true" : "false") + ",";
      json += "\"readyRequired\":" + String(readyRequired ? "true" : "false") + ",";
      json += "\"tapoutEnabled\":" + String(tapoutEnabled ? "true" : "false") + ",";
      json += "\"audioEnabled\":" + String(audioEnabled ? "true" : "false") + ",";
      json += "\"remoteAudioEnabled\":" + String(remoteAudioEnabled ? "true" : "false") + ",";
      json += "\"audioOutput\":" + String(audioOutputSelect) + ",";
        
      json += "\"state\":\"";
      if (currentState == CLOCK_MODE) json += "CLOCK_MODE";
      else if (currentState == RUNNING) json += "RUNNING";
      else if (currentState == PAUSED) json += "PAUSED";
      else if (currentState == PRE_COUNTDOWN_LOOP) json += "PRE_COUNTDOWN_LOOP";
      else if (currentState == FINISHED) json += "FINISHED";
      else if (currentState == TAPOUT) json += "TAPOUT";
      else json += "IDLE";
      json += "\",";

      json += "\"tapoutBlue\":" + String(tapoutInitiatorIsBlue ? "true" : "false") + ",";
      json += "\"red\":" + String(redPaired ? "true" : "false") + ",";
      json += "\"blue\":" + String(bluePaired ? "true" : "false") + ",";
      json += "\"judge\":" + String(judgePaired ? "true" : "false") + ",";
      json += "\"brightness\":" + String(systemBrightness) + ",";
      json += "\"displayInverted\":" + String(displayInverted ? "true" : "false") + ",";
      json += "\"slaveModeEnabled\":" + String(slaveModeEnabled ? "true" : "false") + ",";

      char hexColor[7];
      snprintf(hexColor, sizeof(hexColor), "%02X%02X%02X", digitColor.r, digitColor.g, digitColor.b);
      json += "\"digitColor\":\"" + String(hexColor) + "\",";
      
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
      if (currentState != IDLE && currentState != CLOCK_MODE) {
          server.send(403, "text/plain", "Timer is active!");
          return;
      }
      int h = server.arg("h").toInt();
      int m = server.arg("m").toInt();
      int s = server.arg("s").toInt();

      struct tm tm;
      tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = s;
      tm.tm_year = 2026 - 1900; tm.tm_mon = 4; tm.tm_mday = 10;
      time_t t = mktime(&tm);
      struct timeval now = { .tv_sec = t };
      settimeofday(&now, NULL);
      
      currentState = CLOCK_MODE;

      // Save Clock Mode Active flag to flash
      preferences.begin("settings", false);
      preferences.putBool("clockActive", true);
      preferences.end();

      setBorder();
      server.send(200, "text/plain", "Clock Seeded");
    });

    server.on("/flip", []() {
      displayInverted = !displayInverted;
      preferences.begin("settings", false);
      preferences.putBool("dispInv", displayInverted);
      preferences.end();

      
      needsLEDUpdate = true;
     
      if (currentState == CLOCK_MODE) {
          handleClockMode(); 
      } else {
          updateLEDs();
      }
      
      setBorder();
      server.send(200, "text/plain", displayInverted ? "Inverted" : "Normal");
    });

    server.on("/setaudio", []() {
        if (currentState == RUNNING || currentState == PAUSED || currentState == PRE_COUNTDOWN_LOOP) {
            server.send(403, "text/plain", "Locked during match");
            return;
        }
        
        if (server.hasArg("enabled")) audioEnabled = (server.arg("enabled") == "true");
        if (server.hasArg("remoteEnabled")) remoteAudioEnabled = (server.arg("remoteEnabled") == "true");
        if (server.hasArg("output")) audioOutputSelect = server.arg("output").toInt();

        preferences.begin("settings", false);
        preferences.putBool("audioEnabled", audioEnabled);
        preferences.putBool("remoteAudio", remoteAudioEnabled);
        preferences.putUChar("audioOutput", audioOutputSelect);
        preferences.end();

        server.send(200, "text/plain", "Audio Settings Saved");
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
    String cmd = server.arg("cmd"); //
    
    // --- ADD THIS CRITICAL DEBUGLOG LINE ---
    Serial.printf("[WEB DEBUG] handleControl received endpoint query! cmd = '%s'\n", cmd.c_str());
    
    if (cmd == "readytoggle") { //
        String state = server.arg("state"); //
        readyRequired = (state == "on"); //
        preferences.begin("settings", false); //
        preferences.putBool("readyRequired", readyRequired); //
        preferences.end(); //
        if(currentState != RUNNING) //
          setBorder(); //
    } 
    else if (cmd == "tapouttoggle") { //
        String state = server.arg("state"); //
        tapoutEnabled = (state == "on"); //
        
        preferences.begin("settings", false); //
        preferences.putBool("tapoutEnabled", tapoutEnabled); //
        preferences.end(); //
        
        Serial.printf("[SYSTEM] Tapout Functionality updated and saved: %s\n", tapoutEnabled ? "ENABLED" : "DISABLED"); //
    } 

    else if (cmd == "slavetoggle") {
        String state = server.arg("state");
        slaveModeEnabled = (state == "on");

        preferences.begin("settings", false);
        preferences.putBool("slaveMode", slaveModeEnabled);
        preferences.end();

        Serial.printf("[SYSTEM] Passive Slave configuration updated: %s\n", slaveModeEnabled ? "ACTIVE" : "OFF");
    }
    else {
        processCommand(cmd); //
    }

    server.send(200, "text/plain", "OK"); //
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

    broadcastMasterSync();
    
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
    if (currentState != CONNECTING) return;

    bool connectionFinished = false;

    if (WiFi.status() == WL_CONNECTED) {
        static bool serversStarted = false;
        if (!serversStarted) {
            server.on("/", handleRoot);
            server.on("/control", handleControl);
            server.begin();
            webSocket.begin();
            webSocket.onEvent(onWebSocketEvent);
            
            Serial.println("\n[NETWORK] WiFi Connected successfully!");
            Serial.print("[NETWORK] Local IP Address: http://");
            Serial.println(WiFi.localIP());

            // --- FULL DYNAMIC AUTOMATIC POSIX TIMEZONE DETECTION ---
            HTTPClient http;
            // Fetching raw key-value pair text definitions directly from WorldTimeAPI
            http.begin("http://worldtimeapi.org/api/ip.txt");
            int httpCode = http.GET();
            String posixString = "";
            
            if (httpCode == HTTP_CODE_OK) {
                String payload = http.getString();
                // Find the line that looks like: "abbreviation: CDT" or "abbreviation: CST"
                // WorldTimeAPI text responses explicitly bundle full POSIX strings in the "abbreviation" keys
                int abbrevIdx = payload.indexOf("abbreviation: ");
                
                // Let's grab the precise IANA timezone identifier block as a reliable alternate if needed,
                // but the cleanest automatic method is using their key value formatting.
                // To keep it light, we can use their pre-formatted client target:
                // Let's grab the raw offset definition line if text parsing falls out:
                int tzOffsetIdx = payload.indexOf("utc_offset: ");
                if (tzOffsetIdx != -1) {
                    int startOffset = tzOffsetIdx + 12;
                    int endOffset = payload.indexOf('\n', startOffset);
                    String rawOffset = payload.substring(startOffset, endOffset);
                    rawOffset.trim(); // E.g., "-05:00"
                    
                    // Parse standard offsets directly into a POSIX format the ESP32 can digest instantly:
                    // Note: POSIX rules invert signs (+ is West of GMT, - is East).
                    int hoursOffset = rawOffset.substring(1, 3).toInt();
                    char sign = rawOffset.charAt(0);
                    
                    if (sign == '-') {
                        posixString = "GMT" + String(hoursOffset);
                    } else {
                        posixString = "GMT-" + String(hoursOffset);
                    }
                    
                    // Account for standard US daylight saving rules automatically if in North America
                    if (payload.indexOf("America/") != -1) {
                        // Dynamically append seasonal transition offsets for US regions
                        int dstOffset = hoursOffset - 1;
                        posixString += "GMT+" + String(dstOffset) + ",M3.2.0,M11.1.0";
                    }
                }
            }
            http.end();

            if (posixString.length() > 0) {
                Serial.printf("[NETWORK] Dynamic POSIX Timezone Generated: %s\n", posixString.c_str());
                configTime(0, 0, "pool.ntp.org", "time.nist.gov");
                setenv("TZ", posixString.c_str(), 1);
                tzset();
            } else {
                Serial.println("[NETWORK] Dynamic detection timed out. Defaulting to Central Time rule safely.");
                configTime(0, 0, "pool.ntp.org", "time.nist.gov");
                setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);
                tzset();
            }
            serversStarted = true;
        }

        preferences.begin("settings", true);
        bool clockActive = preferences.getBool("clockActive", false);
        preferences.end();

        if (clockActive) {
            struct tm timeinfo;
            if (getLocalTime(&timeinfo)) {
                Serial.println("[NETWORK] Time successfully synced from NTP server!");
                connectionFinished = true;
            } else {
                static unsigned long lastLog = 0;
                if (millis() - lastLog > 2000) {
                    Serial.println("[NETWORK] Gating startup: Awaiting real-world NTP seed calibration...");
                    lastLog = millis();
                }
            }
        } else {
            connectionFinished = true;
        }
    } 
    else if (millis() - startAttemptTime > 30000) {
        startAPMode();
        connectionFinished = true;
    }

    if (connectionFinished) {
        preferences.begin("settings", true);
        bool clockActive = preferences.getBool("clockActive", false);
        preferences.end();

        if (clockActive) {
            currentState = CLOCK_MODE;
            Serial.println("[PERSISTENCE] Clock fully synchronized. Booting straight to Clock Mode display!");
            
            // 1. Immediately wipe the residual snake frames out of RAM
            for (int i = 0; i < BORDER_LED_COUNT; i++) {
                setBorderLEDs(i, CRGB::Black);
            }
            for (int i = 0; i < DIGIT_LED_COUNT; i++) {
                setDigitLEDs(i, CRGB::Black);
            }
            FastLED.show();

            handleClockMode();
            setBorder();
            FastLED.show(); 

        } else {
            currentState = IDLE;
            setBorder();
            updateClient();
            FastLED.show(); 
        }
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
    preferences.begin("bot-timer", true); 
    redPaired   = (preferences.getBytes("redMAC", redMAC, 6) == 6); 
    bluePaired  = (preferences.getBytes("blueMAC", blueMAC, 6) == 6); 
    judgePaired = (preferences.getBytes("judgeMAC", judgeMAC, 6) == 6); 
    preferences.end(); 
    
    preferences.begin("settings", true); 
    readyRequired = preferences.getBool("readyRequired", false); 
    timeSelState = preferences.getBool("timeSelState", false); 
    systemBrightness = preferences.getUChar("brightness", 127); 
    displayInverted = preferences.getBool("dispInv", false); 
    tapoutEnabled = preferences.getBool("tapoutEnabled", true); 

    audioEnabled = preferences.getBool("audioEnabled", true);
    remoteAudioEnabled = preferences.getBool("remoteAudio", true);
    audioOutputSelect = preferences.getUChar("audioOutput", 0);

    slaveModeEnabled = preferences.getBool("slaveMode", false);
    
    // We remain in CONNECTING state on boot so the loading snake runs normally,
    // but the rest of the network routines will see this flag when finished.
    preferences.end(); 
    FastLED.setBrightness(systemBrightness); 
    uint32_t savedColor = preferences.getUInt("digitColor", 0xFF0000); 
    digitColor = CRGB(savedColor); 

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
  pinMode(BUZZ_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);

  if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    
  esp_now_register_recv_cb(OnDataRecv);
    
  xTaskCreate(
    [](void*) {
        TickType_t lastWakeTime = xTaskGetTickCount();
        while (true) {

          checkAudioTimeout();
          
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

            case TAPOUT:
              handleTapoutAnimation();
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

void broadcastMasterSync() {
    // Slaves never broadcast tracking commands
    if (slaveModeEnabled) return;

    master_sync_message msg;
    msg.packetType = 0xAA;
    msg.systemState = currentState;
    msg.current_time = current_time;
    msg.brightness = systemBrightness;
    msg.displayInverted = displayInverted;
    msg.audioEnabled = audioEnabled;
    msg.remoteAudioEnabled = remoteAudioEnabled;
    msg.tapoutInitiatorIsBlue = tapoutInitiatorIsBlue;

    // Package 24-bit color string hex back to uint32 bitmask
    uint32_t hexColor = ((uint32_t)digitColor.r << 16) | ((uint32_t)digitColor.g << 8) | digitColor.b;
    msg.digitColorHex = hexColor;

    uint8_t broadcastMAC[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(broadcastMAC, (uint8_t *)&msg, sizeof(msg));
}

