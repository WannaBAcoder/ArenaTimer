#include "loraRemotes.h"
#include "config.h"
#include "packet.h"
#include <string.h>

// RA-08H is UART-only on this board (see PCB Files/ESP32_Lora netlist) - no
// SPI/SWD to the module itself. Everything below talks AT commands over
// UART2. The exact P2P receive command/response format is UNCONFIRMED for
// RA-08H's stock firmware - written against the AT+TEST=RXLRPKT convention
// seen on other ASR6601-based modules. Verify against
// Software/Wireless Controllers/TimerRemote/src/radio.cpp's radioDiagnose()
// output once hardware is available, and update both sides together.

static HardwareSerial radioSerial(2); // UART2

static uint8_t redID[4] = {0};
static uint8_t blueID[4] = {0};
static uint8_t judgeID[4] = {0};

static char lineBuf[128];
static size_t lineLen = 0;

static bool hexDecode(const char* hex, size_t hexLen, uint8_t* out, size_t outLen) {
    if (hexLen != outLen * 2) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (size_t i = 0; i < outLen; i++) {
        int hi = nibble(hex[i * 2]);
        int lo = nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static void saveRole(const uint8_t deviceId[4], uint8_t role) {
    preferences.begin("bot-timer", false);
    const char* label = "";

    if (role == ROLE_RED_READY) {
        memcpy(redID, deviceId, 4);
        preferences.putBytes("redID", deviceId, 4);
        redPaired = true;
        label = "RedReady";
    } else if (role == ROLE_BLUE_READY) {
        memcpy(blueID, deviceId, 4);
        preferences.putBytes("blueID", deviceId, 4);
        bluePaired = true;
        label = "BlueReady";
    } else if (role == ROLE_JUDGE) {
        memcpy(judgeID, deviceId, 4);
        preferences.putBytes("judgeID", deviceId, 4);
        judgePaired = true;
        label = "Judge";
    }

    preferences.end();
    Serial.printf("[BIND] %s paired successfully.\n", label);
}

static void handlePacket(const RemotePacket& pkt) {
    if (pairingMode) {
        saveRole(pkt.deviceId, pkt.role);
        pairingMode = false;
        return;
    }

    switch (pkt.role) {
        case ROLE_RED_READY:
            if (redPaired && memcmp(pkt.deviceId, redID, 4) == 0) {
                if (currentState == RUNNING) {
                    queueCommand("tapoutRed");
                } else {
                    queueCommand("readyRed");
                }
            } else {
                Serial.println("[LORA REJECT] Unpaired or unauthorized Red packet source.");
            }
            break;

        case ROLE_BLUE_READY:
            if (bluePaired && memcmp(pkt.deviceId, blueID, 4) == 0) {
                if (currentState == RUNNING) {
                    queueCommand("tapoutBlue");
                } else {
                    queueCommand("readyBlue");
                }
            } else {
                Serial.println("[LORA REJECT] Unpaired or unauthorized Blue packet source.");
            }
            break;

        case ROLE_JUDGE:
            if (judgePaired && memcmp(pkt.deviceId, judgeID, 4) == 0) {
                switch (pkt.buttonId) {
                    case BTN_START: queueCommand("start"); break;
                    case BTN_PAUSE: queueCommand("pause"); break;
                    case BTN_RESET: queueCommand("reset"); break;
                    case BTN_TIME_SEL: queueCommand("switch"); break;
                    case BTN_BUZZER:
                        if (audioEnabled && remoteAudioEnabled) {
                            triggerBeep(250);
                        }
                        break;
                }
            } else {
                Serial.println("[LORA REJECT] Unpaired or unauthorized Judge packet source.");
            }
            break;
    }
}

static void handleRadioLine(const char* line) {
    DEBUG_LOG("[LORA RX] %s\n", line);

    static const char prefix[] = "+TEST: RXLRPKT";
    if (strncmp(line, prefix, sizeof(prefix) - 1) != 0) return; // ignore OK/echo/other lines

    const char* q1 = strchr(line, '"');
    if (!q1) return;
    const char* q2 = strchr(q1 + 1, '"');
    if (!q2) return;

    RemotePacket pkt;
    if (hexDecode(q1 + 1, q2 - (q1 + 1), reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt))) {
        handlePacket(pkt);
    }

    radioSerial.print("AT+TEST=RXLRPKT\r\n"); // re-arm continuous receive
}

void loraInit() {
    radioSerial.begin(115200, SERIAL_8N1, RADIO_UART_RX, RADIO_UART_TX);

    pinMode(RADIO_RST_PIN, OUTPUT);
    digitalWrite(RADIO_RST_PIN, LOW);
    delay(10);
    digitalWrite(RADIO_RST_PIN, HIGH);
    delay(500); // let the module boot before talking to it

    radioSerial.print("AT+TEST=RXLRPKT\r\n"); // arm continuous receive
    Serial.println("[LORA] init complete");
}

void loraPoll() {
    while (radioSerial.available()) {
        char c = radioSerial.read();
        if (c == '\n') {
            lineBuf[lineLen] = '\0';
            if (lineLen > 0) handleRadioLine(lineBuf);
            lineLen = 0;
        } else if (c != '\r' && lineLen < sizeof(lineBuf) - 1) {
            lineBuf[lineLen++] = c;
        }
    }
}

void loraLoadSavedRemotes() {
    preferences.begin("bot-timer", true);
    redPaired = (preferences.getBytes("redID", redID, 4) == 4);
    bluePaired = (preferences.getBytes("blueID", blueID, 4) == 4);
    judgePaired = (preferences.getBytes("judgeID", judgeID, 4) == 4);
    preferences.end();
}

void clearRemotes() {
    Serial.println("[SYSTEM] Wiping remote bindings...");

    preferences.begin("bot-timer", false);
    preferences.remove("redID");
    preferences.remove("blueID");
    preferences.remove("judgeID");
    preferences.end();

    redPaired = false;
    bluePaired = false;
    judgePaired = false;

    memset(redID, 0, 4);
    memset(blueID, 0, 4);
    memset(judgeID, 0, 4);

    Serial.println("[SYSTEM] All remotes wiped.");
}
