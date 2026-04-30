#include <WiFi.h>
#include <esp_now.h>
#include <WebServer.h>
#include <Preferences.h>

// --- CONFIGURATION ---
WebServer server(80);
Preferences prefs;

bool pairingMode = false;
uint8_t redMAC[6], blueMAC[6], judgeMAC[6];
bool redPaired = false, bluePaired = false, judgePaired = false;

typedef struct struct_message {
    char deviceType[15]; 
    int buttonID;
} __attribute__((packed)) struct_message;

struct_message incoming;

// --- 1. ROLE-BASED MEMORY MANAGEMENT ---

void loadSettings() {
    prefs.begin("bot-timer", true);
    redPaired = prefs.getBytes("redMAC", redMAC, 6) == 6;
    bluePaired = prefs.getBytes("blueMAC", blueMAC, 6) == 6;
    judgePaired = prefs.getBytes("judgeMAC", judgeMAC, 6) == 6;
    prefs.end();
}

void saveRole(const uint8_t *mac, String role) {
    prefs.begin("bot-timer", false);
    if (role == "RedReady") {
        memcpy(redMAC, mac, 6);
        prefs.putBytes("redMAC", mac, 6);
        redPaired = true;
    } else if (role == "BlueReady") {
        memcpy(blueMAC, mac, 6);
        prefs.putBytes("blueMAC", mac, 6);
        bluePaired = true;
    } else if (role == "Judge") {
        memcpy(judgeMAC, mac, 6);
        prefs.putBytes("judgeMAC", mac, 6);
        judgePaired = true;
    }
    prefs.end();
    Serial.println("[BIND] " + role + " saved to specific slot.");
}

// --- 2. ESP-NOW HANDLER ---

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
    uint8_t* mac = info->src_addr;
    memcpy(&incoming, data, sizeof(incoming));

    // Pairing Mode: Identifies role IMMEDIATELY and saves to correct slot
    if (pairingMode) { 
        saveRole(mac, String(incoming.deviceType));
        pairingMode = false; 
        return; 
    }
    
    // Authorization: Check if MAC matches any of our three specific roles
    bool isAuthorized = (redPaired && memcmp(mac, redMAC, 6) == 0) || 
                        (bluePaired && memcmp(mac, blueMAC, 6) == 0) || 
                        (judgePaired && memcmp(mac, judgeMAC, 6) == 0);

    if (isAuthorized) {
        String displayName = "Unknown";
        String actionName = "";

        if (strcmp(incoming.deviceType, "RedReady") == 0) {
            displayName = "Red Button";
            actionName = "READY";
        } 
        else if (strcmp(incoming.deviceType, "BlueReady") == 0) {
            displayName = "Blue Button";
            actionName = "READY";
        } 
        else if (strcmp(incoming.deviceType, "Judge") == 0) {
            displayName = "Judge Remote";
            switch(incoming.buttonID) {
                case 1: actionName = "START"; break;
                case 2: actionName = "PAUSE"; break;
                case 3: actionName = "RESET"; break;
                case 4: actionName = "TIME SELECT"; break;
            }
        }
        Serial.printf("[EVENT] %s -> %s\n", displayName.c_str(), actionName.c_str());
    }
}

// --- 3. WEB UI ---

void handleRoot() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'>";
    if (pairingMode) html += "<meta http-equiv='refresh' content='2'>";
    html += "</head><body style='font-family:sans-serif; text-align:center;'>";
    html += "<h1>BATTLE TIMER HOST</h1>";
    
    String netStatus = (WiFi.status() == WL_CONNECTED) ? "Connected: " + WiFi.SSID() : "Mode: Setup (AP)";
    html += "<div style='margin-bottom:10px;'>" + netStatus + "</div>";

    if (pairingMode) html += "<div style='background:blue; color:white; padding:10px;'>PAIRING... Press the remote button now.</div>";

    html += "<div style='margin:15px auto; padding:15px; border:1px solid #ccc; border-radius:8px; width:fit-content; text-align:left; display:inline-block;'>";
    
    // These now check the SPECIFIC boolean for that role
    html += "<b>Red Button: </b>" + String(redPaired ? "<span style='color:green;'>PAIRED</span>" : "<span style='color:red;'>OPEN</span>") + "<br>";
    html += "<b>Blue Button: </b>" + String(bluePaired ? "<span style='color:green;'>PAIRED</span>" : "<span style='color:red;'>OPEN</span>") + "<br>";
    html += "<b>Judge Remote: </b>" + String(judgePaired ? "<span style='color:green;'>PAIRED</span>" : "<span style='color:red;'>OPEN</span>") + "<br>";
    
    html += "</div><br>";
    html += "<a href='/pair'><button style='padding:10px 20px;'>START PAIRING MODE</button></a> ";
    html += "<a href='/clear_remotes'><button style='background:orange; padding:10px 20px;'>WIPE ALL</button></a><hr>";
    
    html += "<h3>WiFi Config</h3><form action='/savewifi' method='POST'>";
    html += "SSID: <input type='text' name='ssid'><br>Pass: <input type='password' name='pass'><br>";
    html += "<input type='submit' value='SAVE' style='margin-top:10px;'></form></body></html>";
    server.send(200, "text/html", html);
}

// --- 4. SETUP & LOOP ---

void setup() {
    Serial.begin(115200);
    loadSettings();

    prefs.begin("bot-timer", true);
    String s = prefs.getString("wifi_ssid", "");
    String p = prefs.getString("wifi_pass", "");
    prefs.end();

    WiFi.mode(WIFI_AP_STA);
    if (s != "") WiFi.begin(s.c_str(), p.c_str());
    WiFi.softAP("BattleTimer_Setup", "password123");

    esp_now_init();
    esp_now_register_recv_cb(OnDataRecv);

    server.on("/", handleRoot);
    server.on("/pair", []() { pairingMode = true; server.sendHeader("Location", "/"); server.send(302); });
    server.on("/clear_remotes", []() { 
        prefs.begin("bot-timer", false); 
        prefs.remove("redMAC"); prefs.remove("blueMAC"); prefs.remove("judgeMAC");
        prefs.end();
        redPaired = bluePaired = judgePaired = false;
        server.sendHeader("Location", "/"); server.send(302); 
    });
    server.on("/savewifi", HTTP_POST, [](){
        prefs.begin("bot-timer", false);
        prefs.putString("wifi_ssid", server.arg("ssid"));
        prefs.putString("wifi_pass", server.arg("pass"));
        prefs.end();
        server.send(200, "text/html", "Rebooting...");
        delay(1000); ESP.restart();
    });

    server.begin();
}

void loop() { server.handleClient(); }