#include "rceManager.h"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"
#include "timerLogic.h"

// Protocol notes (live-v1.md / timer-control-v1.md): ActionCable-style JSON
// over wss://<host>/cable. Server frames are either control frames at the top
// level ({"type":"welcome"}, ping, confirm_subscription, disconnect, ...) or
// data messages wrapped as {"identifier":..,"message":{<envelope>}}. The
// envelope carries v/type/id/seq/event/arena/at/data. The docs describe the
// wrapping loosely, so the parser below accepts the envelope either nested
// under "message" or at the top level.

// Threading: a network task pinned to core 0 owns the WebSocket (connect, TLS
// handshake, reads, writes, reconnect back-off). It never touches timer or
// display state - it only moves frames between the socket and two queues:
//   rxQueue: received text frames, parsed and applied by rceLoop() on the
//            main loop's core;
//   txQueue: outgoing frames queued by rceLoop()/rceNotifyLocal().
// A slow TLS handshake or an unreachable host therefore can't stall the LED
// animation or the clock, and a match in progress never has to wait for the
// connection.

extern String timerName;

volatile bool rceApplying = false;

extern const uint8_t rootca_crt_bundle_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t rootca_crt_bundle_end[] asm("_binary_x509_crt_bundle_end");

namespace {

// --- configuration (NVS namespace "rce") -----------------------------------
String cfgHost = "www.rcemanager.com";
uint16_t cfgPort = 443;
bool cfgTls = true;
String cfgKey = "";
String cfgArena = "";
String cfgDevice = "";

// --- connection state --------------------------------------------------------
WebSocketsClient ws;          // used only from the network task (except setReconnectInterval)
volatile bool wsConnected = false;   // TCP+WebSocket handshake done (network task writes)
bool subscribed = false;      // confirm_subscription received (main loop only)
bool haveSnapshot = false;
bool halted = false;          // server said "unauthorized, don't retry"
bool noMatch = true;
volatile uint32_t lastRxMs = 0;
uint8_t backoffIdx = 0;
uint32_t lastSeq = 0;
uint32_t lastVersion = 0;     // timer.version is per match, so this is only meaningful for lastMatchId
uint32_t lastMatchId = 0;
bool pendingReconcile = false;  // pushing a locally-running match to the server after an outage
uint32_t reconcileSince = 0;
uint32_t cmdCounter = 0;
String identifier;            // exact subscribe identifier string, echoed in every frame
String lastError = "";
String lastCommandSent = "";
char authHeader[200];

// --- network task plumbing -------------------------------------------------------
struct RxItem {
    char* data;       // malloc'd, NUL-terminated; the consumer frees it
    uint32_t epoch;   // connection generation it arrived on
};
QueueHandle_t rxQueue = nullptr;
QueueHandle_t txQueue = nullptr;
TaskHandle_t netTask = nullptr;
volatile uint32_t connEpoch = 0;     // bumped on every new connection; stale rx items are dropped
volatile bool netRunning = false;    // network task has begun the socket
volatile uint8_t netReq = 0;         // main -> network task: 1 = (re)start, 2 = stop
volatile bool rxOverflow = false;    // a frame was dropped; ask the server to resync

bool enqueueTx(const String& frame) {
    if (!txQueue) return false;
    char* copy = strdup(frame.c_str());
    if (!copy) return false;
    if (xQueueSend(txQueue, &copy, 0) != pdTRUE) {
        free(copy);
        return false;
    }
    return true;
}

// --- what RCE Manager last told us about the clock ------------------------------
char serverState[12] = "";
uint32_t serverEndMs = 0;     // millis() at which the server's running clock hits zero
bool authIsUs = false;
String authText = "RCE Manager";

const uint32_t BACKOFF_MS[] = {1000, 2000, 4000, 8000, 16000, 30000};

// The JSON filter keeps ArduinoJson's working set small: a one-arena snapshot
// is 5-6 KB, but we only need names, timers and authority out of it.
JsonDocument filterDoc;
bool filterBuilt = false;

void fillEnvelopeFilter(JsonObject o) {
    o["type"] = true;
    o["seq"] = true;
    o["arena"] = true;
    JsonObject d = o["data"].to<JsonObject>();
    d["command_id"] = true;
    d["command"] = true;
    d["error"] = true;
    d["message"] = true;
    d["duplicate"] = true;
    d["timer"] = true;
    JsonObject a = d["arena"].to<JsonObject>();
    a["name"] = true;
    a["timer"] = true;
    a["authority"] = true;
    JsonObject sa = d["arenas"][0].to<JsonObject>();
    sa["name"] = true;
    sa["timer"] = true;
    sa["authority"] = true;
}

void buildFilter() {
    if (filterBuilt) return;
    filterDoc["type"] = true;
    filterDoc["reason"] = true;
    filterDoc["reconnect"] = true;
    filterDoc["identifier"] = true;
    JsonObject wrapped = filterDoc["message"].to<JsonObject>();
    fillEnvelopeFilter(wrapped);
    // Same fields at the top level in case the envelope is not wrapped.
    JsonObject top = filterDoc.as<JsonObject>();
    top["seq"] = true;
    top["arena"] = true;
    JsonObject d = top["data"].to<JsonObject>();
    d["command_id"] = true;
    d["command"] = true;
    d["error"] = true;
    d["message"] = true;
    d["duplicate"] = true;
    d["timer"] = true;
    JsonObject a = d["arena"].to<JsonObject>();
    a["name"] = true;
    a["timer"] = true;
    a["authority"] = true;
    JsonObject sa = d["arenas"][0].to<JsonObject>();
    sa["name"] = true;
    sa["timer"] = true;
    sa["authority"] = true;
    filterBuilt = true;
}

String defaultDevice() {
    if (timerName.length() > 0) return timerName;
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char buf[24];
    snprintf(buf, sizeof(buf), "ArenaTimer-%02X%02X", mac[4], mac[5]);
    return String(buf);
}

String effectiveDevice() {
    String d = cfgDevice.length() > 0 ? cfgDevice : defaultDevice();
    if (d.length() > 40) d = d.substring(0, 40);
    return d;
}

void logHeap(const char* where) {
    Serial.printf("[RCE] heap free=%u min=%u (%s)\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(), where);
}

String nextCommandId() {
    char buf[24];
    snprintf(buf, sizeof(buf), "t%06X", (unsigned)(cmdCounter++ & 0xFFFFFF));
    return String(buf);
}

bool sendCommand(const char* command, int remaining) {
    if (!subscribed) return false;

    JsonDocument inner;
    inner["action"] = "timer";
    inner["arena"] = cfgArena;
    inner["command"] = command;
    inner["id"] = nextCommandId();
    if (lastVersion > 0) inner["version"] = lastVersion;
    if (remaining >= 0) inner["remaining_s"] = remaining;
    String innerStr;
    serializeJson(inner, innerStr);

    JsonDocument outer;
    outer["command"] = "message";
    outer["identifier"] = identifier;
    outer["data"] = innerStr;
    String out;
    serializeJson(outer, out);

    lastCommandSent = String(command);
    DEBUG_LOG("[RCE] -> %s\n", out.c_str());
    return enqueueTx(out);
}

int stateCode(const char* s) {
    if (!strcmp(s, "idle")) return IDLE;
    if (!strcmp(s, "running")) return RUNNING;
    if (!strcmp(s, "paused")) return PAUSED;
    if (!strcmp(s, "finished")) return FINISHED;
    return -1;
}

void updateAuthority(JsonVariantConst a) {
    if (a.isNull()) return;
    const char* type = a["type"] | "";
    if (!strcmp(type, "device")) {
        const char* dev = a["device"] | "";
        authIsUs = (effectiveDevice() == String(dev));
        authText = String(dev);
    } else {
        authIsUs = false;
        authText = "RCE Manager";
    }
}

// force: bypass the version check (used for ack/error replies, which carry
// the current timer even when its version is not newer than ours).
void handleTimer(JsonVariantConst t, bool snapshot, bool force) {
    if (t.isNull()) {
        noMatch = true;
        lastMatchId = 0;
        lastVersion = 0;
        return;
    }
    noMatch = false;

    // "version increments on every change to this match's timer": it restarts
    // for each new match, so remembering the highest value ever seen would
    // make every later match look stale. Track it per match.
    uint32_t matchId = t["match_id"] | 0;
    if (matchId != lastMatchId) {
        lastMatchId = matchId;
        lastVersion = 0;
    }

    uint32_t version = t["version"] | 0;
    const char* st = t["state"] | "";
    int remaining = (int)lroundf(t["remaining_s"].as<float>());
    int duration = t["duration_s"] | 0;
    updateAuthority(t["authority"]);

    if (!snapshot && !force && version != 0 && version <= lastVersion) return;
    // A snapshot replaces all local state, including what we believed the
    // version to be (e.g. after the server restarted).
    if (snapshot || version > lastVersion) lastVersion = version;

    strlcpy(serverState, st, sizeof(serverState));
    serverEndMs = millis() + (uint32_t)(remaining < 0 ? 0 : remaining) * 1000UL;

    int code = stateCode(st);
    if (code < 0) return;   // unknown state: "treat as something else"

    const char* srcType = t["last_source"]["type"] | "";
    const char* srcDev = t["last_source"]["device"] | "";
    bool ours = !strcmp(srcType, "integration") && effectiveDevice() == String(srcDev);

    // Mid-reconcile (see the snapshot case below): we sent `adjust` with our
    // own time and are waiting for its echo before sending `start`. Anything
    // else - the server caught up, someone else acted, or it took too long -
    // ends the reconcile and the normal path below takes over.
    if (pendingReconcile) {
        if (millis() - reconcileSince > 5000 || currentState != RUNNING || code == RUNNING) {
            pendingReconcile = false;
        } else if (ours && (code == IDLE || code == PAUSED)) {
            sendCommand("start", current_time);
            return;
        } else {
            pendingReconcile = false;
        }
    }

    // Our own change, echoed back. When we run the clock our value is the
    // truth and there is nothing to apply.
    if (!snapshot && ours && authIsUs) return;

    // A snapshot normally replaces local state, but a physical timer must
    // never wipe a live match: if this one was started locally while the
    // connection was down, push it to the server instead of reverting it.
    //  - clock authority: `start` carries our remaining time and wins;
    //  - otherwise the server ignores remaining_s on start, so first set its
    //    remaining time with `adjust` (allowed while idle or paused), then
    //    send `start` once that is echoed back (pendingReconcile).
    if (snapshot && currentState == RUNNING && (code == IDLE || code == PAUSED)) {
        if (authIsUs) {
            sendCommand("start", current_time);
        } else {
            sendCommand("adjust", current_time);
            pendingReconcile = true;
            reconcileSince = millis();
        }
        return;
    }
    if (snapshot && authIsUs && currentState == PAUSED && code == RUNNING) {
        sendCommand("pause", current_time);
        return;
    }

    // A running clock only needs re-aligning if it is actually off.
    if (code == RUNNING && currentState == RUNNING && abs(current_time - remaining) <= 1) return;
    if (code == PAUSED && currentState == PAUSED && current_time == remaining) return;
    if (code == IDLE && currentState == IDLE && current_time == remaining) return;
    if (code == FINISHED && currentState == FINISHED) return;

    Serial.printf("[RCE] apply state=%s remaining=%d duration=%d v=%u%s\n", st, remaining, duration,
                  (unsigned)version, snapshot ? " (snapshot)" : "");
    rceApplying = true;
    rceApplyExternal(code, remaining, duration);
    rceApplying = false;
}

void handleArena(JsonVariantConst a, bool snapshot) {
    if (a.isNull()) return;
    updateAuthority(a["authority"]);
    handleTimer(a["timer"], snapshot, false);
}

void sendSubscribe() {
    JsonDocument id;
    id["channel"] = "IntegrationChannel";
    id["arenas"][0] = cfgArena;
    id["device"] = effectiveDevice();
    identifier = "";
    serializeJson(id, identifier);

    JsonDocument sub;
    sub["command"] = "subscribe";
    sub["identifier"] = identifier;
    String out;
    serializeJson(sub, out);
    enqueueTx(out);
    DEBUG_LOG("[RCE] -> %s\n", out.c_str());
}

void onFrame(const char* payload, size_t length) {
    buildFilter();
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload, length, DeserializationOption::Filter(filterDoc));
    if (err) {
        Serial.printf("[RCE] bad JSON (%s), %u bytes\n", err.c_str(), (unsigned)length);
        return;
    }

    // Control frames sit at the top level.
    const char* type = doc["type"] | "";
    if (!strcmp(type, "ping")) return;
    if (!strcmp(type, "welcome")) {
        sendSubscribe();
        return;
    }
    if (!strcmp(type, "confirm_subscription")) {
        subscribed = true;
        Serial.println("[RCE] subscribed");
        return;
    }
    if (!strcmp(type, "reject_subscription")) {
        subscribed = false;
        lastError = "subscribe rejected (check arena name)";
        Serial.println("[RCE] subscription rejected");
        return;
    }
    if (!strcmp(type, "disconnect")) {
        const char* reason = doc["reason"] | "";
        bool reconnect = doc["reconnect"] | true;
        lastError = String("server disconnect: ") + reason;
        Serial.printf("[RCE] server disconnect: %s (reconnect=%d)\n", reason, reconnect);
        if (!reconnect) {
            halted = true;
            ws.setReconnectInterval(0xFFFFFFFFUL);
        }
        return;
    }

    JsonVariantConst env = doc.as<JsonVariantConst>();
    if (doc["message"].is<JsonObject>()) env = doc["message"].as<JsonVariantConst>();
    const char* mtype = env["type"] | "";
    JsonVariantConst data = env["data"];

    if (!strcmp(mtype, "snapshot")) {
        JsonVariantConst arenas = data["arenas"];
        JsonVariantConst mine;
        for (JsonVariantConst a : arenas.as<JsonArrayConst>()) {
            if (cfgArena == String((const char*)(a["name"] | ""))) { mine = a; break; }
        }
        lastSeq = env["seq"] | 0;
        haveSnapshot = true;
        // The snapshot arrives before confirm_subscription, but it only
        // exists because the subscription was accepted - and it can need an
        // immediate reply (reconciling a match started during an outage).
        subscribed = true;
        backoffIdx = 0;
        ws.setReconnectInterval(BACKOFF_MS[0]);
        if (mine.isNull()) {
            noMatch = true;
            lastError = "arena not in snapshot";
        } else {
            handleArena(mine, true);
        }
        logHeap("snapshot");
        return;
    }

    if (!strcmp(mtype, "ack")) {
        handleTimer(data["timer"], false, true);
        return;
    }
    if (!strcmp(mtype, "error")) {
        const char* code = data["error"] | "error";
        lastError = String(code) + " (" + lastCommandSent + ")";
        Serial.printf("[RCE] command refused: %s\n", lastError.c_str());
        // Every 409 carries the current timer - resynchronise from it.
        handleTimer(data["timer"], false, true);
        return;
    }

    // Change messages: apply only if newer than what we have.
    JsonVariantConst seqV = env["seq"];
    if (!seqV.isNull()) {
        uint32_t seq = seqV.as<uint32_t>();
        if (seq <= lastSeq) return;
        lastSeq = seq;
    }
    const char* arena = env["arena"] | "";
    if (arena[0] != '\0' && cfgArena != String(arena)) return;

    if (!strncmp(mtype, "timer.", 6) || !strncmp(mtype, "match.", 6)) {
        handleTimer(data["timer"], false, false);
    } else if (!strcmp(mtype, "queue.updated")) {
        handleArena(data["arena"], false);
    }
}

// Runs on the network task (called from ws.loop()). Must stay light: flags
// and queue pushes only - no JSON, no timer or display access.
void onWsEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            connEpoch = connEpoch + 1;
            lastRxMs = millis();
            wsConnected = true;
            Serial.println("[RCE] connected");
            // The server speaks first (welcome); the main loop subscribes on receipt.
            break;
        case WStype_DISCONNECTED:
            if (wsConnected) Serial.println("[RCE] disconnected");
            wsConnected = false;
            if (!halted) {
                uint8_t idx = backoffIdx < 5 ? backoffIdx : 5;
                ws.setReconnectInterval(BACKOFF_MS[idx] + (esp_random() % 500));
                if (backoffIdx < 5) backoffIdx++;
            }
            break;
        case WStype_TEXT: {
            lastRxMs = millis();
            if (!rxQueue) break;
            char* copy = (char*)malloc(length + 1);
            if (!copy) {
                rxOverflow = true;
                break;
            }
            memcpy(copy, payload, length);
            copy[length] = '\0';
            RxItem item = {copy, connEpoch};
            if (xQueueSend(rxQueue, &item, 0) != pdTRUE) {
                free(copy);
                rxOverflow = true;
            }
            break;
        }
        case WStype_PING:
        case WStype_PONG:
            lastRxMs = millis();
            break;
        case WStype_ERROR:
            Serial.println("[RCE] websocket error");
            break;
        default:
            break;
    }
}

// Main loop: forget everything about the previous session before a (re)start.
void resetSessionState() {
    lastSeq = 0;
    lastVersion = 0;
    lastMatchId = 0;
    halted = false;
    backoffIdx = 0;
    pendingReconcile = false;
    noMatch = true;
    serverState[0] = '\0';
    authIsUs = false;
    authText = "RCE Manager";
    lastError = "";
    subscribed = false;
    haveSnapshot = false;
    if (cmdCounter == 0) cmdCounter = esp_random() & 0xFFFFFF;
    snprintf(authHeader, sizeof(authHeader), "Authorization: Bearer %s", cfgKey.c_str());
}

// Network task: open the socket with the current settings.
void beginSocket() {
    Serial.printf("[RCE] connecting to %s://%s:%u/cable arena='%s' device='%s'\n", cfgTls ? "wss" : "ws",
                  cfgHost.c_str(), cfgPort, cfgArena.c_str(), effectiveDevice().c_str());
    if (cfgTls) {
        ws.beginSslWithBundle(cfgHost.c_str(), cfgPort, "/cable", rootca_crt_bundle_start,
                              rootca_crt_bundle_end - rootca_crt_bundle_start, "actioncable-v1-json");
    } else {
        ws.begin(cfgHost.c_str(), cfgPort, "/cable", "actioncable-v1-json");
    }
    ws.setExtraHeaders(authHeader);
    ws.onEvent(onWsEvent);
    ws.setReconnectInterval(BACKOFF_MS[0]);
}

void netTaskFn(void*) {
    bool started = false;
    for (;;) {
        uint8_t req = netReq;
        if (req == 2) {
            netReq = 0;
            if (started) ws.disconnect();
            started = false;
            netRunning = false;
            wsConnected = false;
        } else if (req == 1) {
            netReq = 0;
            beginSocket();
            started = true;
            netRunning = true;
        }

        char* out = nullptr;
        if (started && WiFi.status() == WL_CONNECTED) {
            ws.loop();   // may block for a TCP/TLS connect - only this task waits

            // live-v1: no frame for 10 s means the connection is dead.
            if (wsConnected && millis() - lastRxMs > 10000) {
                Serial.println("[RCE] no frames for 10 s, reconnecting");
                ws.disconnect();
            }
            while (xQueueReceive(txQueue, &out, 0) == pdTRUE) {
                if (wsConnected) ws.sendTXT(out);
                free(out);
            }
        } else {
            if (wsConnected) {   // WiFi dropped under an open socket
                ws.disconnect();
                wsConnected = false;
            }
            while (xQueueReceive(txQueue, &out, 0) == pdTRUE) free(out);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void ensureNetTask() {
    if (netTask) return;
    rxQueue = xQueueCreate(8, sizeof(RxItem));
    txQueue = xQueueCreate(6, sizeof(char*));
    xTaskCreatePinnedToCore(netTaskFn, "rceNet", 16384, nullptr, 1, &netTask, 0);
}

void drainRx(bool discard) {
    RxItem item;
    int budget = 4;   // bound the work done per loop() pass
    while ((discard || budget-- > 0) && rxQueue && xQueueReceive(rxQueue, &item, 0) == pdTRUE) {
        if (!discard && item.epoch == connEpoch) onFrame(item.data, strlen(item.data));
        free(item.data);
    }
}

void requestStop() {
    if (netTask && netRunning) netReq = 2;
}

}  // namespace

// --- public API ------------------------------------------------------------------

void rceLoadConfig() {
    Preferences p;
    p.begin("rce", true);
    cfgHost = p.getString("host", "www.rcemanager.com");
    cfgPort = p.getUShort("port", 443);
    cfgTls = p.getBool("tls", true);
    cfgKey = p.getString("key", "");
    cfgArena = p.getString("arena", "");
    cfgDevice = p.getString("device", "");
    p.end();
}

bool rceModeActive() {
    return cfgKey.length() > 0 && cfgArena.length() > 0;
}

bool rceSetConfig(const String& host, uint16_t port, bool tls, const String& key,
                  const String& arena, const String& device) {
    String a = arena;
    a.trim();
    String d = device;
    d.trim();
    String h = host;
    h.trim();
    if (a.length() == 0 || a.length() > 60) return false;
    if (d.length() > 40) return false;
    if (h.length() == 0) h = "www.rcemanager.com";

    cfgHost = h;
    cfgPort = port ? port : 443;
    cfgTls = tls;
    if (key.length() > 0) cfgKey = key;
    cfgArena = a;
    cfgDevice = d;

    Preferences p;
    p.begin("rce", false);
    p.putString("host", cfgHost);
    p.putUShort("port", cfgPort);
    p.putBool("tls", cfgTls);
    p.putString("key", cfgKey);
    p.putString("arena", cfgArena);
    p.putString("device", cfgDevice);
    p.end();

    requestStop();   // rceLoop() restarts it with the new settings
    return true;
}

void rceClearConfig() {
    requestStop();
    cfgKey = "";
    cfgArena = "";
    cfgDevice = "";
    cfgHost = "www.rcemanager.com";
    cfgPort = 443;
    cfgTls = true;
    Preferences p;
    p.begin("rce", false);
    p.clear();
    p.end();
}

void rceLoop() {
    bool want = rceModeActive() && WiFi.status() == WL_CONNECTED;
    if (!want) {
        if (netTask && netRunning) requestStop();
        if (netTask) drainRx(true);
        subscribed = haveSnapshot = false;
        return;
    }

    ensureNetTask();
    if (!netRunning && netReq == 0) {
        resetSessionState();
        netReq = 1;
    }

    // A dropped connection ends the subscription; the next connection starts
    // over from the server's welcome and snapshot.
    if (!wsConnected && (subscribed || haveSnapshot)) {
        subscribed = false;
        haveSnapshot = false;
    }

    drainRx(false);

    if (rxOverflow && subscribed) {
        rxOverflow = false;
        JsonDocument d;
        d["action"] = "resync";
        String inner;
        serializeJson(d, inner);
        JsonDocument o;
        o["command"] = "message";
        o["identifier"] = identifier;
        o["data"] = inner;
        String out;
        serializeJson(o, out);
        enqueueTx(out);
        Serial.println("[RCE] dropped a frame, requested resync");
    }

    uint32_t now = millis();

    // timer-control-v1: while this device runs the clock, correct RCE
    // Manager when the two clocks differ by 2 s or more (and not more often
    // than needed - every adjust redraws every timer screen).
    static uint32_t lastDriftCheck = 0;
    static uint32_t lastAdjust = 0;
    if (subscribed && authIsUs && currentState == RUNNING && !strcmp(serverState, "running") &&
        now - lastDriftCheck >= 1000) {
        lastDriftCheck = now;
        long ours = (long)timerRemainingMs();
        long theirs = (long)(serverEndMs - now);
        if (labs(ours - theirs) >= 2000 && now - lastAdjust >= 5000) {
            lastAdjust = now;
            Serial.printf("[RCE] drift %ld ms, sending adjust\n", ours - theirs);
            sendCommand("adjust", (int)((ours + 500) / 1000));
        }
    }
}

void rceNotifyLocal(RceEvent ev) {
    if (!rceModeActive() || rceApplying || !subscribed) return;
    switch (ev) {
        case RCE_EV_START: sendCommand("start", current_time); break;
        case RCE_EV_PAUSE: sendCommand("pause", current_time); break;
        case RCE_EV_RESET: sendCommand("reset", -1); break;
        case RCE_EV_FINISH:
            // Pausing at 0 ends the match - only meaningful for the clock
            // authority; otherwise RCE Manager finishes it on its own.
            if (authIsUs) sendCommand("pause", 0);
            break;
    }
}

String rceStatusJson() {
    String status;
    if (!rceModeActive()) {
        if (cfgArena.length() > 0 && cfgKey.length() == 0) status = "off (integration key needed)";
        else if (cfgKey.length() > 0 && cfgArena.length() == 0) status = "off (arena name needed)";
        else status = "off";
    }
    else if (WiFi.status() != WL_CONNECTED) status = "waiting for WiFi";
    else if (halted) status = "key rejected";
    else if (subscribed) status = noMatch ? "online, no match called" : "online";
    else if (wsConnected) status = "connected, subscribing";
    else status = "connecting";

    String j = "{";
    j += "\"mode\":" + String(rceModeActive() ? "true" : "false") + ",";
    j += "\"status\":\"" + status + "\",";
    j += "\"online\":" + String(subscribed ? "true" : "false") + ",";
    j += "\"authority\":\"" + (authIsUs ? String("this timer") : authText) + "\",";
    j += "\"runsClock\":" + String(authIsUs ? "true" : "false") + ",";
    j += "\"host\":\"" + cfgHost + "\",";
    j += "\"port\":" + String(cfgPort) + ",";
    j += "\"tls\":" + String(cfgTls ? "true" : "false") + ",";
    j += "\"keySet\":" + String(cfgKey.length() > 0 ? "true" : "false") + ",";
    j += "\"arena\":\"" + cfgArena + "\",";
    j += "\"device\":\"" + effectiveDevice() + "\",";
    String err = lastError;
    err.replace("\"", "'");
    j += "\"lastError\":\"" + err + "\",";
    j += "\"heapFree\":" + String(ESP.getFreeHeap()) + ",";
    j += "\"heapMin\":" + String(ESP.getMinFreeHeap());
    j += "}";
    return j;
}
