#include "radio.h"
#include "config.h"
#include <Arduino.h>

// USART1: PA9 (TX, -> module LPRXD) / PA10 (RX, <- module TXD)
static HardwareSerial radioSerial(PIN_RADIO_RX, PIN_RADIO_TX);
// One-way debug output only, to J2 pin 5. No RX wired, hence NC.
static HardwareSerial debugSerial(NC, PIN_DEBUG_TX);

void radioInit() {
    debugSerial.begin(115200);
    radioSerial.begin(115200);

    pinMode(PIN_RADIO_RST, OUTPUT);
    digitalWrite(PIN_RADIO_RST, LOW);
    delay(10);
    digitalWrite(PIN_RADIO_RST, HIGH);
    delay(500); // let the module boot before talking to it

    debugSerial.println("[radio] init complete");
}

size_t radioSendATCommand(const char* cmd, char* respBuf, size_t bufLen, uint32_t timeoutMs) {
    while (radioSerial.available()) radioSerial.read(); // flush stale bytes

    radioSerial.print(cmd);
    radioSerial.print("\r\n");

    size_t received = 0;
    uint32_t start = millis();
    while (millis() - start < timeoutMs && received < bufLen - 1) {
        if (radioSerial.available()) {
            respBuf[received++] = radioSerial.read();
        }
    }
    respBuf[received] = '\0';
    return received;
}

void radioDiagnose() {
    char resp[160];

    debugSerial.println("[radio] === diagnostic probe ===");

    debugSerial.println("[radio] AT+CGMI? (baseline LoRaWAN AT sanity check)");
    radioSendATCommand("AT+CGMI?", resp, sizeof(resp), 1000);
    debugSerial.println(resp);

    debugSerial.println("[radio] AT+TEST=? (P2P test-mode probe, unconfirmed for RA-08H)");
    radioSendATCommand("AT+TEST=?", resp, sizeof(resp), 1000);
    debugSerial.println(resp);

    debugSerial.println("[radio] AT+CTX=? (transparent-transmission-firmware probe)");
    radioSendATCommand("AT+CTX=?", resp, sizeof(resp), 1000);
    debugSerial.println(resp);

    debugSerial.println("[radio] === end diagnostic probe ===");
}

// PLACEHOLDER: implemented against the AT+TEST=TXLRPKT convention used by
// other ASR6601-based modules. Not confirmed for RA-08H's stock firmware -
// adjust once radioDiagnose() results are known (see radio.h).
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
    size_t n = radioSendATCommand(cmd, resp, sizeof(resp), 2000);
    debugSerial.println(resp);
    return n > 0;
}
