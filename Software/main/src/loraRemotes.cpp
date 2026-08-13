#include "loraRemotes.h"
#include "config.h"
#include "packet.h"
#include "rfConfig.h"
#include <string.h>

// RA-08H is UART-only on this board (see PCB Files/ESP32_Lora netlist) - no
// SPI/SWD to the module itself. Everything below talks AT commands over
// UART2, targeting the custom P2P firmware in Software/RA08H_p2p_bridge
// (stock RA-08H firmware is LoRaWAN-only and has no device-to-device path).
// Protocol confirmed on hardware - see that project's README.

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

// Sends one AT command line (CRLF appended) and blocks for a CRLF-terminated
// response, up to timeoutMs. Only used at boot for AT+RFCFG - the ongoing
// RX/pairing path in loraPoll() is intentionally non-blocking and doesn't
// use this.
static size_t sendRadioATCommand(const char* cmd, char* respBuf, size_t bufLen, uint32_t timeoutMs) {
    while (radioSerial.available()) radioSerial.read(); // flush stale bytes

    radioSerial.print(cmd);
    radioSerial.print("\r\n");

    size_t received = 0;
    uint32_t start = millis();
    while (millis() - start < timeoutMs && received < bufLen - 1) {
        if (radioSerial.available()) {
            respBuf[received++] = radioSerial.read();
            if (received >= 2 && respBuf[received - 2] == '\r' && respBuf[received - 1] == '\n') {
                break;
            }
        }
    }
    respBuf[received] = '\0';
    return received;
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

    // No re-arm command needed: RA08H_p2p_bridge re-arms Radio.Rx() itself on
    // every packet/timeout, and has no "AT+TEST=RXLRPKT" command at all -
    // sending one just draws a harmless but noisy "ERROR" reply from it.
}

void loraInit() {
    // 9600, not 115200: the module's AT interface sits on its LPUART, whose
    // baud rate cannot exceed 9600. This is a hardware limit of the RA-08H,
    // not a preference - see Software/RA08H_p2p_bridge/README.md.
    radioSerial.begin(RADIO_BAUD, SERIAL_8N1, RADIO_UART_RX, RADIO_UART_TX);

    pinMode(RADIO_RST_PIN, OUTPUT);
    digitalWrite(RADIO_RST_PIN, LOW);
    delay(10);
    digitalWrite(RADIO_RST_PIN, HIGH);
    delay(500); // let the module boot before talking to it

    // The radio doesn't persist RF settings across resets - it always comes
    // up on its own compiled-in defaults - so push the desired config (see
    // rfConfig.h) on every boot. To retune, edit rfConfig.h and reflash the
    // ESP32 (USB, no BOOT-jumper dance) rather than the radio itself.
    //
    // Retried rather than trusting a single fixed delay: the radio's own
    // boot sequence can take longer than the 500ms above in practice, and
    // this is the one command that has to land before the radio is fully
    // booted - confirmed on hardware (STM32 side) that a single attempt at
    // 500ms can time out.
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+RFCFG=%d,%d,%d,%d", RF_TX_POWER, RF_SF, RF_BW, RF_CR);
    char resp[64];
    bool rfCfgOk = false;
    for (int attempt = 0; attempt < 5 && !rfCfgOk; attempt++) {
        sendRadioATCommand(cmd, resp, sizeof(resp), 300);
        rfCfgOk = (strstr(resp, "OK") != nullptr);
        if (!rfCfgOk) delay(200);
    }
    Serial.printf("[LORA] RFCFG %s -> %s\n", cmd, rfCfgOk ? "OK" : "FAILED after retries");

    // No arm command needed: p2p_bridge calls Radio.Rx() itself once init
    // finishes (see Software/RA08H_p2p_bridge/src/main.c) and has no
    // "AT+TEST=RXLRPKT" command to send one to anyway.
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
