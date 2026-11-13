#include "loraRemotes.h"
#include "config.h"
#include "packet.h"
#include "rfConfig.h"
#include "radioFlash.h"
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

static bool radioPresent = false;
bool loraRadioPresent() { return radioPresent; }

// Last battery report / last time anything was heard, per role (index =
// role - 1: judge, red, blue). Only ever set for the PAIRED remote of that
// role, and RAM-only - after a timer reboot they read "never" until the
// remote's next status frame or button press.
static uint16_t remoteBatteryMv[3] = {0, 0, 0};
static uint32_t remoteHeardMs[3] = {0, 0, 0};
static bool remoteHeardEver[3] = {false, false, false};

static void markHeard(uint8_t role) {
    if (role < 1 || role > 3) return;
    remoteHeardMs[role - 1] = millis();
    remoteHeardEver[role - 1] = true;
}

uint16_t loraBatteryMv(uint8_t role) {
    return (role >= 1 && role <= 3) ? remoteBatteryMv[role - 1] : 0;
}

int32_t loraSecondsSinceHeard(uint8_t role) {
    if (role < 1 || role > 3 || !remoteHeardEver[role - 1]) return -1;
    return (int32_t)((millis() - remoteHeardMs[role - 1]) / 1000UL);
}

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

// Sends one boot-time AT command, retrying briefly rather than trusting a
// single fixed delay: the radio's own boot sequence can take longer than
// the delay before this is first called, and every boot-time command has to
// land before the radio is fully up - confirmed on hardware (STM32 side)
// that a single attempt right after reset can time out.
static bool sendBootCommandWithRetry(const char* cmd, const char* label) {
    char resp[64];
    bool ok = false;
    for (int attempt = 0; attempt < 5 && !ok; attempt++) {
        sendRadioATCommand(cmd, resp, sizeof(resp), 300);
        ok = (strstr(resp, "OK") != nullptr);
        if (!ok) delay(200);
    }
    Serial.printf("[LORA] %s %s -> %s\n", label, cmd, ok ? "OK" : "FAILED after retries");
    return ok;
}

static void saveRole(const uint8_t deviceId[4], uint8_t role) {
    preferences.begin("bot-timer", false);
    const char* label = "";

    if (role == ROLE_RED_READY) {
        memcpy(redID, deviceId, 4);
        preferences.putBytes("redID", deviceId, 4);
        redLoraPaired = true;
        label = "RedReady";
    } else if (role == ROLE_BLUE_READY) {
        memcpy(blueID, deviceId, 4);
        preferences.putBytes("blueID", deviceId, 4);
        blueLoraPaired = true;
        label = "BlueReady";
    } else if (role == ROLE_JUDGE) {
        memcpy(judgeID, deviceId, 4);
        preferences.putBytes("judgeID", deviceId, 4);
        judgeLoraPaired = true;
        label = "Judge";
    }

    preferences.end();
    recomputePairedFlags();
    Serial.printf("[BIND] %s paired successfully (LoRa).\n", label);
}

static void handlePacket(const RemotePacket& pkt) {
    if (pairingMode) {
        saveRole(pkt.deviceId, pkt.role);
        pairingMode = false;
        return;
    }

    switch (pkt.role) {
        case ROLE_RED_READY:
            if (redLoraPaired && memcmp(pkt.deviceId, redID, 4) == 0) {
                markHeard(ROLE_RED_READY);
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
            if (blueLoraPaired && memcmp(pkt.deviceId, blueID, 4) == 0) {
                markHeard(ROLE_BLUE_READY);
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
            if (judgeLoraPaired && memcmp(pkt.deviceId, judgeID, 4) == 0) {
                markHeard(ROLE_JUDGE);
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

// Battery/status frame from a remote (see packet.h). Never used for pairing -
// only a real button press pairs a remote - and only recorded for a remote
// that is already paired to this timer.
static void handleStatus(const RemoteStatusPacket& pkt) {
    if (pkt.buttonId != BTN_STATUS) return;

    const uint8_t* expected = nullptr;
    bool paired = false;
    switch (pkt.role) {
        case ROLE_RED_READY: expected = redID; paired = redLoraPaired; break;
        case ROLE_BLUE_READY: expected = blueID; paired = blueLoraPaired; break;
        case ROLE_JUDGE: expected = judgeID; paired = judgeLoraPaired; break;
        default: return;
    }
    if (!paired || memcmp(pkt.deviceId, expected, 4) != 0) return;

    remoteBatteryMv[pkt.role - 1] = batteryCodeToMv(pkt.battery);
    markHeard(pkt.role);
    DEBUG_LOG("[LORA] role %u battery %u mV\n", pkt.role, batteryCodeToMv(pkt.battery));
}

static void handleRadioLine(const char* line) {
    DEBUG_LOG("[LORA RX] %s\n", line);

    static const char prefix[] = "+TEST: RXLRPKT";
    if (strncmp(line, prefix, sizeof(prefix) - 1) != 0) return; // ignore OK/echo/other lines

    const char* q1 = strchr(line, '"');
    if (!q1) return;
    const char* q2 = strchr(q1 + 1, '"');
    if (!q2) return;

    // Frames are told apart by length: 6 bytes = button press (the original
    // format), 7 bytes = status frame. Anything else is ignored, so a timer
    // running this code also ignores any frame type added in the future.
    const size_t hexLen = q2 - (q1 + 1);
    if (hexLen == sizeof(RemotePacket) * 2) {
        RemotePacket pkt;
        if (hexDecode(q1 + 1, hexLen, reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt))) {
            handlePacket(pkt);
        }
    } else if (hexLen == sizeof(RemoteStatusPacket) * 2) {
        RemoteStatusPacket pkt;
        if (hexDecode(q1 + 1, hexLen, reinterpret_cast<uint8_t*>(&pkt), sizeof(pkt))) {
            handleStatus(pkt);
        }
    }

    // No re-arm command needed: RA08H_p2p_bridge re-arms Radio.Rx() itself on
    // every packet/timeout, and has no "AT+TEST=RXLRPKT" command at all -
    // sending one just draws a harmless but noisy "ERROR" reply from it.
}

static void resetRadio() {
    // BOOT low starts the app image. The board's external pull-down is the real
    // hold; the internal one is a backstop if that resistor is missing.
    pinMode(RADIO_BOOT_PIN, INPUT_PULLDOWN);
    pinMode(RADIO_RST_PIN, OUTPUT);
    digitalWrite(RADIO_RST_PIN, LOW);
    delay(10);
    digitalWrite(RADIO_RST_PIN, HIGH);
    delay(500); // let the module boot before talking to it
}

// True if the module answers like Software/RA08H_p2p_bridge. Only that
// firmware replies "+RFCFG:" to AT+RFCFG? - the stock LoRaWAN image, or a
// blank module (silent), does not. Collects a short window of output rather
// than the first line so a boot-log line is not mistaken for the reply.
static bool radioIsP2p() {
    char resp[96];
    for (int attempt = 0; attempt < 5; attempt++) {
        while (radioSerial.available()) radioSerial.read();
        radioSerial.print("AT+RFCFG?\r\n");
        size_t n = 0;
        uint32_t start = millis();
        while (millis() - start < 250 && n < sizeof(resp) - 1) {
            if (radioSerial.available()) resp[n++] = radioSerial.read();
        }
        resp[n] = '\0';
        if (strstr(resp, "+RFCFG:") != nullptr) return true;
        delay(150);
    }
    return false;
}

void loraInit() {
    // 9600, not 115200: the module's AT interface sits on its LPUART, whose
    // baud rate cannot exceed 9600. This is a hardware limit of the RA-08H,
    // not a preference - see Software/RA08H_p2p_bridge/README.md.
    radioSerial.begin(RADIO_BAUD, SERIAL_8N1, RADIO_UART_RX, RADIO_UART_TX);

    resetRadio();

#ifdef RADIO_FLASH_TEST
    // Bench test (env:radiotest): exercises the whole flash path on a radio
    // that may already be programmed. Erase it, prove it went silent, flash
    // it, prove it came back. Results are on one summary line at the end.
    {
        bool before = radioIsP2p();
        Serial.printf("[TEST] 1 radio answers before: %s\n", before ? "yes" : "no");
        bool erased = radioEraseFirmware(radioSerial);
        resetRadio();
        bool blank = !radioIsP2p();
        Serial.printf("[TEST] 2 erase ok=%d, radio silent afterwards=%d\n", erased, blank);
        bool flashed = radioFlashFirmware(radioSerial);
        resetRadio();
        bool back = radioIsP2p();
        Serial.printf("[TEST] 3 flash ok=%d, radio answers again=%d\n", flashed, back);
        Serial.printf("[TEST] RESULT: %s\n", (erased && blank && flashed && back) ? "PASS" : "FAIL");
    }
#endif

#ifndef DISABLE_RADIO_AUTOFLASH
    // Unprogrammed (or stock-firmware) radio: program the embedded P2P image,
    // then restart it and check again. On a board without a radio or without
    // the BOOT/RXD wiring the bootloader never answers; that costs a couple of
    // seconds here and the code below then falls back to ESP-NOW only.
    if (!radioIsP2p()) {
        Serial.println("[LORA] no P2P firmware detected");
        if (radioFlashFirmware(radioSerial)) {
            resetRadio();
            if (!radioIsP2p()) Serial.println("[LORA] still no answer after flashing");
        }
    }
#endif

    // The radio doesn't persist RF settings across resets - it always comes
    // up on its own compiled-in defaults - so push the desired config (see
    // rfConfig.h) on every boot. To retune, edit rfConfig.h and reflash the
    // ESP32 (USB, no BOOT-jumper dance) rather than the radio itself.
    char rfCfgCmd[48];
    snprintf(rfCfgCmd, sizeof(rfCfgCmd), "AT+RFCFG=%d,%d,%d,%d", RF_TX_POWER, RF_SF, RF_BW, RF_CR);
    radioPresent = sendBootCommandWithRetry(rfCfgCmd, "RFCFG");

    // No radio answered any of RFCFG's retries - this unit doesn't have the
    // RA-08H installed (older ESP-NOW-only hardware sharing this firmware
    // image). Skip RXMODE's own retry budget too rather than spending it on
    // a radio that was already shown not to be there, and leave loraPoll()
    // a no-op for the rest of runtime.
    if (!radioPresent) {
        Serial.println("[LORA] No radio detected - running ESP-NOW only.");
        return;
    }

    char rxModeCmd[24];
    snprintf(rxModeCmd, sizeof(rxModeCmd), "AT+RXMODE=%d", RF_RX_MODE);
    sendBootCommandWithRetry(rxModeCmd, "RXMODE");

    // No arm command needed: p2p_bridge calls Radio.Rx() itself once init
    // finishes (see Software/RA08H_p2p_bridge/src/main.c) and has no
    // "AT+TEST=RXLRPKT" command to send one to anyway.
    Serial.println("[LORA] init complete");
}

void loraPoll() {
    if (!radioPresent) return;

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
    redLoraPaired = (preferences.getBytes("redID", redID, 4) == 4);
    blueLoraPaired = (preferences.getBytes("blueID", blueID, 4) == 4);
    judgeLoraPaired = (preferences.getBytes("judgeID", judgeID, 4) == 4);
    preferences.end();
    recomputePairedFlags();
}

void loraClearRemotes() {
    preferences.begin("bot-timer", false);
    preferences.remove("redID");
    preferences.remove("blueID");
    preferences.remove("judgeID");
    preferences.end();

    redLoraPaired = false;
    blueLoraPaired = false;
    judgeLoraPaired = false;

    memset(redID, 0, 4);
    memset(blueID, 0, 4);
    memset(judgeID, 0, 4);

    memset(remoteBatteryMv, 0, sizeof(remoteBatteryMv));
    memset(remoteHeardEver, 0, sizeof(remoteHeardEver));
}
