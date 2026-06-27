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

TaskHandle_t TimerTaskHandle = NULL;

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
  memcpy(&incoming, data, sizeof(incoming)); 
  uint8_t* mac = info->src_addr; 

  // If it's a buzzer hold packet, we BYPASS the 300ms cooldown so we track the stream.
  static unsigned long lastPacketTime = 0;
  if (incoming.buttonID != 5) { 
      if (millis() - lastPacketTime < 300) return;
      lastPacketTime = millis();
      Serial.printf("[ESP-NOW] Role: %s | Button: %d\n", incoming.deviceType, incoming.buttonID);
  }

  if (pairingMode) { 
      saveRole(mac, String(incoming.deviceType)); 
      pairingMode = false; 
      return; 
  }

  // --- WIRELESS REMOTE LOGIC ---
  if (strcmp(incoming.deviceType, "RedReady") == 0) { 
      if (currentState == RUNNING) { 
          processCommand("tapoutRed");
      } else {
          setTeamReady("Red"); 
      }
  } 
  else if (strcmp(incoming.deviceType, "BlueReady") == 0) { 
      if (currentState == RUNNING) { 
          processCommand("tapoutBlue");
      } else {
          setTeamReady("Blue"); 
      }
  }
  else if (strcmp(incoming.deviceType, "Judge") == 0) { 
    switch(incoming.buttonID) { 
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

    server.on("/", handleRoot);
    server.on("/control", handleControl);
    server.on("/settime", handleSetTime);
    server.on("/setwifi", handleSetWiFi);
    
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
      uint32_t hex = strtol(hexStr.c_str(), NULL, 16);
      digitColor = CRGB(hex);
      
      preferences.begin("settings", false);
      preferences.putUInt("digitColor", hex);
      preferences.end();
      
      updateLEDs(); 
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
        int hour = server.arg("h").toInt();
        int minute = server.arg("m").toInt();
        int s = server.arg("s").toInt();

        struct tm tm;
        tm.tm_hour = hour; tm.tm_min = minute; tm.tm_sec = s;
        tm.tm_year = 2026 - 1900; tm.tm_mon = 4; tm.tm_mday = 10;
        time_t t = mktime(&tm);
        struct timeval now = { .tv_sec = t };
        settimeofday(&now, NULL);
        
        if (TimerTaskHandle != NULL) vTaskSuspend(TimerTaskHandle);
        currentState = CLOCK_MODE;

        preferences.begin("settings", false);
        preferences.putBool("clockActive", true);
        preferences.end();

        for (int i = 0; i < BORDER_LED_COUNT; i++) setBorderLEDs(i, CRGB::Black);
        for (int i = 0; i < DIGIT_LED_COUNT; i++) setDigitLEDs(i, CRGB::Black);

        if (hour == 0) hour = 12;
        if (hour > 12) hour -= 12;

        if (!displayInverted) {
            setDigit(hour / 10, 0, false);
            setDigit(hour % 10, 49, false);
            setColon();
            setDigit(minute / 10, 150, true);
            setDigit(minute % 10, 101, true);
        } else {
            setDigit(minute % 10, 0, false);
            setDigit(minute / 10, 49, false);
            setColon();
            setDigit(hour % 10, 150, true);
            setDigit(hour / 10, 101, true);
        }

        for (int i = 0; i < BORDER_LED_COUNT; i++)
            setBorderLEDs(i, ORANGE); 

        FastLED.show();
        needsLEDUpdate = false;

        if (TimerTaskHandle != NULL) vTaskResume(TimerTaskHandle);
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
    Serial.printf("[WEB DEBUG] handleControl received endpoint query! cmd = '%s'\n", cmd.c_str());
    
    if (cmd == "readytoggle") { 
        String state = server.arg("state"); 
        readyRequired = (state == "on"); 
        preferences.begin("settings", false); 
        preferences.putBool("readyRequired", readyRequired); 
        preferences.end(); 
        if(currentState != RUNNING) 
          setBorder(); 
    } 
    else if (cmd == "tapouttoggle") { 
        String state = server.arg("state"); 
        tapoutEnabled = (state == "on"); 
        
        preferences.begin("settings", false); 
        preferences.putBool("tapoutEnabled", tapoutEnabled); 
        preferences.end(); 
        
        Serial.printf("[SYSTEM] Tapout Functionality updated and saved: %s\n", tapoutEnabled ? "ENABLED" : "DISABLED"); 
    } 
    else {
        processCommand(cmd); 
    }
    server.send(200, "text/plain", "OK"); 
}

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

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {}

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
    startAttemptTime = millis(); 
    currentState = CONNECTING;   
}

void checkWiFiConnection() {
    if (currentState != CONNECTING) return;

    bool connectionFinished = false;
    bool connectionFailed = false; 

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

            configTime(0, 0, "pool.ntp.org", "time.nist.gov");
            setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);
            tzset();

            serversStarted = true;
        }
        connectionFinished = true;
    } 
    else if (millis() - startAttemptTime > 30000) {
        startAPMode();
        connectionFinished = true;
        connectionFailed = true; 
    }

    if (connectionFinished) {
        preferences.begin("settings", true);
        bool clockActive = preferences.getBool("clockActive", false);
        preferences.end();

        currentState = IDLE;
        setBorder();
        updateClient();
        FastLED.show();

        if (clockActive && !connectionFailed) {
            Serial.println("[NETWORK] Saved Clock Mode detected. Queuing background calibration...");
            
            xTaskCreate(
                [](void*) {
                    vTaskDelay(pdMS_TO_TICKS(500)); 

                    Serial.println("[ASYNC TZ] Performing web timezone detection...");
                    HTTPClient http;
                    http.setTimeout(4000); 
                    http.begin("http://worldtimeapi.org/api/ip.txt");
                    
                    int httpCode = http.GET();
                    String posixString = "";
                    
                    if (httpCode == HTTP_CODE_OK) {
                        String payload = http.getString();
                        int tzOffsetIdx = payload.indexOf("utc_offset: ");
                        if (tzOffsetIdx != -1) {
                            int startOffset = tzOffsetIdx + 12;
                            int endOffset = payload.indexOf('\n', startOffset);
                            String rawOffset = payload.substring(startOffset, endOffset);
                            rawOffset.trim();
                            
                            int hoursOffset = rawOffset.substring(1, 3).toInt();
                            char sign = rawOffset.charAt(0);
                            
                            if (sign == '-') {
                                posixString = "GMT" + String(hoursOffset);
                            } else {
                                posixString = "GMT-" + String(hoursOffset);
                            }
                            
                            if (payload.indexOf("America/") != -1) {
                                int dstOffset = hoursOffset - 1;
                                posixString += "GMT+" + String(dstOffset) + ",M3.2.0,M11.1.0";
                            }
                        }
                    }
                    http.end();

                    if (posixString.length() > 0) {
                        Serial.printf("[ASYNC TZ] Found matching region rule: %s\n", posixString.c_str());
                        setenv("TZ", posixString.c_str(), 1);
                        tzset();
                    } else {
                        Serial.println("[ASYNC TZ] Network timeout. Staying with Central Time rule.");
                    }

                    struct tm timeinfo;
                    unsigned long startWait = millis();
                    while (!getLocalTime(&timeinfo) && (millis() - startWait < 8000)) {
                        vTaskDelay(pdMS_TO_TICKS(500));
                    }

                    if (TimerTaskHandle != NULL) vTaskSuspend(TimerTaskHandle);
                    currentState = CLOCK_MODE;
                    
                    for (int i = 0; i < BORDER_LED_COUNT; i++) setBorderLEDs(i, CRGB::Black);
                    for (int i = 0; i < DIGIT_LED_COUNT; i++) setDigitLEDs(i, CRGB::Black);
                    
                    handleClockMode();
                    setBorder();
                    
                    FastLED.show();
                    needsLEDUpdate = false;
                    
                    if (TimerTaskHandle != NULL) vTaskResume(TimerTaskHandle);
                    Serial.println("[SYSTEM] Seamless boot transition to Clock Mode complete.");

                    vTaskDelete(NULL); 
                }, "BootClockTransitionTask", 4096, nullptr, 1, nullptr
            );
        } 
        else if (connectionFailed) {
            Serial.println("[SYSTEM WARNING] WiFi Connection failed! Gating in IDLE mode on Access Point hotspot.");
        }
    }
}

void clearRemotes() {
    Serial.println("[SYSTEM] Wiping remote bindings...");
    
    preferences.begin("bot-timer", false); 
    preferences.remove("redMAC"); 
    preferences.remove("blueMAC"); 
    preferences.remove("judgeMAC");
    preferences.end();
    
    redPaired = false;
    bluePaired = false;
    judgePaired = false;
    
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
    
    preferences.end(); 
    FastLED.setBrightness(systemBrightness); 
    uint32_t savedColor = preferences.getUInt("digitColor", 0xFF0000); 
    digitColor = CRGB(savedColor); 

    countdown_time = timeSelState ? 180 : 120; 
    current_time = countdown_time; 
}

void setup() {
  Serial.begin(115200);

  loadSavedSettings();
  initDisplay();
  initNetwork();
  
  updateLEDs();
  setBorder();
  FastLED.show();

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
        // Core loop state execution
        TickType_t lastWakeTime = xTaskGetTickCount();
        while (true) {
          checkAudioTimeout();
          
          switch(currentState) {
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

          if (needsLEDUpdate) {
              FastLED.show();
              needsLEDUpdate = false;
          }
          vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1));
        }
    },"TimerTask",4096, nullptr, 1, &TimerTaskHandle
  );

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