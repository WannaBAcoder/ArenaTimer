#include <WiFi.h>
#include <esp_now.h>
#include <WebServer.h>
#include <Preferences.h>

WebServer server(80);
Preferences prefs;

bool pairingMode = false;
uint8_t boundDevices[5][6];
int deviceCount = 0;
bool isAPMode = false;

typedef struct struct_message {
    char deviceType[15];
    int buttonID;
} struct_message;

struct_message incoming;

// --- 1. SURGICAL MEMORY MANAGEMENT ---

void loadSettings() {
    prefs.begin("bot-timer", true);
    deviceCount = prefs.getInt("count", 0);
    for (int i = 0; i < deviceCount; i++) {
        String key = "mac" + String(i);
        prefs.getBytes(key.c_str(), boundDevices[i], 6);
    }
    prefs.end();
    Serial.printf("\n[BOOT] Bound Remotes: %d/5\n", deviceCount);
}

void saveDevice(const uint8_t *mac) {
    if (deviceCount >= 5) return;
    for(int i=0; i<deviceCount; i++) {
        if(memcmp(boundDevices[i], mac, 6) == 0) return;
    }
    prefs.begin("bot-timer", false);
    memcpy(boundDevices[deviceCount], mac, 6);
    String key = "mac" + String(deviceCount);
    prefs.putBytes(key.c_str(), mac, 6);
    deviceCount++;
    prefs.putInt("count", deviceCount);
    prefs.end();
    Serial.println("[BIND] Remote saved.");
}

// --- 2. ESP-NOW HANDLER ---

void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
    uint8_t* mac = info->src_addr;
    if (pairingMode) { saveDevice(mac); pairingMode = false; return; }
    
    bool authorized = false;
    for (int i = 0; i < deviceCount; i++) {
        if (memcmp(boundDevices[i], mac, 6) == 0) authorized = true;
    }

    if (authorized) {
        memcpy(&incoming, data, sizeof(incoming));
        Serial.printf("[EVENT] %s -> Button %d\n", incoming.deviceType, incoming.buttonID);
    }
}

// --- 3. WEB UI & WIFI LOGIC ---

void handleSaveWiFi() {
    String q_ssid = server.arg("ssid");
    String q_pass = server.arg("pass");
    
    WiFi.begin(q_ssid.c_str(), q_pass.c_str());
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) { delay(100); yield(); }

    if (WiFi.status() == WL_CONNECTED) {
        prefs.begin("bot-timer", false);
        prefs.putString("wifi_ssid", q_ssid);
        prefs.putString("wifi_pass", q_pass);
        prefs.end();
        server.send(200, "text/plain", "SUCCESS! Connected. Credentials saved.");
        isAPMode = false;
    } else {
        server.send(200, "text/plain", "FAILED to connect. Flash NOT updated.");
        WiFi.softAP("BattleTimer_Setup", "password123");
    }
}

void handleRoot() {
    String html = "<html><body style='font-family:sans-serif; text-align:center;'>";
    html += "<h1>BATTLE TIMER HOST</h1>";
    html += "<h3>Remotes Linked: " + String(deviceCount) + "/5</h3>";
    html += "<a href='/pair'><button>PAIR REMOTE</button></a> ";
    html += "<a href='/clear_remotes'><button style='background:orange;'>WIPE REMOTES ONLY</button></a><hr>";
    html += "<h3>WiFi Config</h3>";
    html += "<form action='/savewifi' method='POST'>";
    html += "SSID: <input type='text' name='ssid'><br><br>";
    html += "Pass: <input type='text' name='pass'> (VISIBLE)<br><br>";
    html += "<input type='submit' value='TEST & SAVE'>";
    html += "</form></body></html>";
    server.send(200, "text/html", html);
}

// --- 4. SETUP & LOOP ---

void setup() {
    Serial.begin(115200);
    loadSettings();

    prefs.begin("bot-timer", true);
    String storedSSID = prefs.getString("wifi_ssid", "");
    String storedPass = prefs.getString("wifi_pass", "");
    prefs.end();

    if (storedSSID != "") {
        WiFi.begin(storedSSID.c_str(), storedPass.c_str());
        unsigned long start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) { delay(500); Serial.print("."); }
    }

    if (WiFi.status() == WL_CONNECTED) {
        isAPMode = false;
        Serial.println("\n[WIFI] Connected to " + storedSSID);
    } else {
        isAPMode = true;
        WiFi.disconnect();
        WiFi.softAP("BattleTimer_Setup", "password123");
        Serial.println("\n[WIFI] Started AP: BattleTimer_Setup");
    }

    esp_now_init();
    esp_now_register_recv_cb(OnDataRecv);

    server.on("/", handleRoot);
    server.on("/savewifi", HTTP_POST, handleSaveWiFi);
    server.on("/pair", []() { pairingMode = true; server.send(200, "text/plain", "Pairing..."); });
    
    // THE NEW SURGICAL CLEAR
    server.on("/clear_remotes", []() { 
        prefs.begin("bot-timer", false);
        prefs.remove("count"); // Delete the counter
        for(int i=0; i<5; i++) { prefs.remove(("mac" + String(i)).c_str()); } // Delete all 5 slots
        prefs.end();
        server.send(200, "text/plain", "Remotes cleared. WiFi kept. Rebooting...");
        delay(1000); ESP.restart(); 
    });

    server.begin();
}

void loop() { server.handleClient(); }