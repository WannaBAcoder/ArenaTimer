#include "radio.h"
#include "config.h"
#include "rfConfig.h"
#include <Arduino.h>
#include <string.h>

// USART1: PA9 (TX, -> module LPRXD) / PA10 (RX, <- module TXD)
static HardwareSerial radioSerial(PIN_RADIO_RX, PIN_RADIO_TX);
// One-way debug output only, to J2 pin 5. No RX wired, hence NC.
static HardwareSerial debugSerial(NC, PIN_DEBUG_TX);

void radioInit() {
    debugSerial.begin(115200);
    // 9600, not 115200: the module's AT interface sits on its LPUART, whose
    // baud rate cannot exceed 9600. Hardware limit of the RA-08H, not a
    // preference - see Software/RA08H_p2p_bridge/README.md.
    radioSerial.begin(RADIO_BAUD);

    pinMode(PIN_RADIO_RST, OUTPUT);
    digitalWrite(PIN_RADIO_RST, LOW);
    delay(10);
    digitalWrite(PIN_RADIO_RST, HIGH);
    delay(500); // let the module boot before talking to it

    // The radio doesn't persist RF settings across resets - it always comes
    // up on its own compiled-in defaults - so push the desired config (see
    // rfConfig.h) on every boot. To retune, edit rfConfig.h and reflash the
    // STM32 (fast, ST-Link only) rather than the radio itself.
    //
    // Retried rather than trusting a single fixed delay: the radio's own
    // boot sequence (oscillator startup, RTC init, radio hardware init) can
    // take longer than the 500ms above in practice, and this is the one
    // command that has to land before the radio is fully booted - every
    // other AT command (a button-press TXLRPKT) happens long after boot, so
    // this timing edge never surfaced until RFCFG was added. Confirmed on
    // hardware: a single attempt at 500ms can time out.
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+RFCFG=%d,%d,%d,%d", RF_TX_POWER, RF_SF, RF_BW, RF_CR);
    char resp[64];
    bool rfCfgOk = false;
    for (int attempt = 0; attempt < 5 && !rfCfgOk; attempt++) {
        radioSendATCommand(cmd, resp, sizeof(resp), 300);
        rfCfgOk = (strstr(resp, "OK") != nullptr);
        if (!rfCfgOk) delay(200);
    }

    debugSerial.print("[radio] RFCFG ");
    debugSerial.print(cmd);
    debugSerial.println(rfCfgOk ? " -> OK" : " -> FAILED after retries");
    debugSerial.println("[radio] init complete");
}

size_t radioSendATCommand(const char* cmd, char* respBuf, size_t bufLen, uint32_t timeoutMs) {
    while (radioSerial.available()) radioSerial.read(); // flush stale bytes

    radioSerial.print(cmd);
    radioSerial.print("\r\n");

    // p2p_bridge always replies with one CRLF-terminated line (OK/ERROR), so
    // return as soon as that line is complete instead of busy-waiting the
    // full timeout on every call - the radio typically replies in well under
    // a millisecond, but this function used to block for the entire
    // timeoutMs regardless, stalling loop() (and therefore every other
    // button) for up to 2s per press. timeoutMs is now purely a fallback for
    // a genuinely unresponsive radio.
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

// Targets the custom P2P firmware in Software/RA08H_p2p_bridge. Confirmed on
// hardware: the module replies OK and the packet is received on the far end.
bool radioSendPacket(const uint8_t* data, size_t len) {
    char hexPayload[64];
    size_t hexLen = 0;
    for (size_t i = 0; i < len && hexLen + 2 < sizeof(hexPayload); i++) {
        static const char hexDigits[] = "0123456789ABCDEF";
        hexPayload[hexLen++] = hexDigits[(data[i] >> 4) & 0xF];
        hexPayload[hexLen++] = hexDigits[data[i] & 0xF];
    }
    hexPayload[hexLen] = '\0';

    char cmd[96];
    snprintf(cmd, sizeof(cmd), "AT+TEST=TXLRPKT,\"%s\"", hexPayload);

    char resp[96];
    radioSendATCommand(cmd, resp, sizeof(resp), 2000);

    bool ok = (strstr(resp, "OK") != nullptr);
    debugSerial.print("[radio] TX ");
    debugSerial.print(hexPayload);
    debugSerial.println(ok ? " -> OK" : " -> NO ACK");
    return ok;
}
