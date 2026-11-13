#include "radioFlash.h"
#include "radio.h"
#include "config.h"
#include "radio_image.h"
#include <Arduino.h>
#include <string.h>

// ASR6601 ROM bootloader framing (see tremo_loader.py):
//   request : 0xFE, cmd, len16 LE, data[len], crc32 LE, 0xEF
//   response: 0xFE, status, len16 LE, data[len], crc32 LE, 0xEF
// CRC32 is zlib's, taken over everything before the CRC field.
//
// UNTESTED ON HARDWARE as of writing: ported line-for-line from
// tremo_loader.py, which is the verified reference.

#define LOADER_BAUD 921600
#define CMD_SYNC 1
#define CMD_FLASH 3
#define CMD_ERASE 4
#define CMD_VERIFY 5
#define CMD_REBOOT 12
#define CMD_BAUDRATE 16
#define CHUNK 512

static uint8_t txBuf[4 + 8 + CHUNK + 5];
static uint8_t rxBuf[32];
static uint8_t payloadBuf[8 + CHUNK];

static uint32_t crc32Calc(const uint8_t* d, size_t n) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= d[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void putLE32(uint8_t* p, uint32_t v) {
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}

static void sendRequest(uint8_t cmd, const uint8_t* payload, size_t n) {
    HardwareSerial& ser = radioPort();
    while (ser.available()) ser.read();
    txBuf[0] = 0xFE;
    txBuf[1] = cmd;
    txBuf[2] = n & 0xFF;
    txBuf[3] = n >> 8;
    if (n) memcpy(&txBuf[4], payload, n);
    putLE32(&txBuf[4 + n], crc32Calc(txBuf, 4 + n));
    txBuf[4 + n + 4] = 0xEF;
    ser.write(txBuf, 4 + n + 5);
    ser.flush();
}

// Returns the response status byte (0 = success), or -1 on a timeout /
// framing / CRC error.
static int waitResponse(uint32_t timeoutMs) {
    HardwareSerial& ser = radioPort();
    ser.setTimeout(timeoutMs);
    if (ser.readBytes(rxBuf, 4) != 4) return -1;
    if (rxBuf[0] != 0xFE) return -1;
    uint8_t status = rxBuf[1];
    uint16_t len = rxBuf[2] | (rxBuf[3] << 8);
    if ((size_t)len + 9 > sizeof(rxBuf)) return -1;
    if (ser.readBytes(&rxBuf[4], len + 5) != (size_t)(len + 5)) return -1;
    uint32_t crc = rxBuf[4 + len] | (rxBuf[5 + len] << 8) | (rxBuf[6 + len] << 16) |
                   ((uint32_t)rxBuf[7 + len] << 24);
    if (rxBuf[8 + len] != 0xEF) return -1;
    if (crc != crc32Calc(rxBuf, 4 + len)) return -1;
    return status;
}

static bool command(uint8_t cmd, const uint8_t* payload, size_t n, uint32_t timeoutMs) {
    sendRequest(cmd, payload, n);
    return waitResponse(timeoutMs) == 0;
}

// BOOT high across the radio's reset puts the ROM bootloader in charge.
static void resetIntoBootloader() {
    pinMode(PIN_RADIO_BOOT, OUTPUT);
    digitalWrite(PIN_RADIO_BOOT, HIGH);
    pinMode(PIN_RADIO_RST, OUTPUT);
    digitalWrite(PIN_RADIO_RST, LOW);
    delay(100);
    digitalWrite(PIN_RADIO_RST, HIGH);
    delay(100);
    digitalWrite(PIN_RADIO_BOOT, LOW);
}

static void releaseBoot() {
    digitalWrite(PIN_RADIO_BOOT, LOW);
    pinMode(PIN_RADIO_BOOT, INPUT); // R20 holds it low
}

bool radioFlashFirmware() {
    HardwareSerial& dbg = debugPort();
    HardwareSerial& ser = radioPort();
    dbg.println("[flash] programming radio");

    ser.end();
    ser.begin(LOADER_BAUD);

    bool synced = false;
    for (int attempt = 0; attempt < 3 && !synced; attempt++) {
        resetIntoBootloader();
        for (int i = 0; i < 4 && !synced; i++) {
            synced = command(CMD_SYNC, nullptr, 0, 400);
        }
    }

    bool ok = false;
    if (!synced) {
        dbg.println("[flash] bootloader did not answer");
    } else {
        uint8_t arg[12];

        // tremo_loader.py always issues this after connecting (a no-op when
        // the baud is unchanged), so do the same.
        putLE32(arg, LOADER_BAUD);
        ok = command(CMD_BAUDRATE, arg, 4, 1000);
        if (!ok) dbg.println("[flash] baud command rejected");

        if (ok) {
            putLE32(arg, RADIO_IMAGE_FLASH_ADDR);
            putLE32(arg + 4, RADIO_IMAGE_LEN);
            ok = command(CMD_ERASE, arg, 8, 8000);
            if (!ok) dbg.println("[flash] erase failed");
        }

        for (uint32_t off = 0; ok && off < RADIO_IMAGE_LEN; off += CHUNK) {
            uint32_t n = RADIO_IMAGE_LEN - off;
            if (n > CHUNK) n = CHUNK;
            putLE32(payloadBuf, RADIO_IMAGE_FLASH_ADDR + off);
            putLE32(payloadBuf + 4, n);
            memcpy(payloadBuf + 8, &RADIO_IMAGE[off], n);
            ok = command(CMD_FLASH, payloadBuf, 8 + n, 5000);
            if (!ok) {
                dbg.print("[flash] write failed at ");
                dbg.println(off);
            }
        }

        if (ok) {
            putLE32(arg, RADIO_IMAGE_FLASH_ADDR);
            putLE32(arg + 4, RADIO_IMAGE_LEN);
            putLE32(arg + 8, RADIO_IMAGE_CRC32);
            ok = command(CMD_VERIFY, arg, 12, 8000);
            if (!ok) dbg.println("[flash] verify failed");
        }

        if (ok) {
            uint8_t mode = 0;
            command(CMD_REBOOT, &mode, 1, 1000); // radio resets itself; the reply may not arrive
        }
    }

    releaseBoot();
    ser.end();
    ser.begin(RADIO_BAUD);
    dbg.println(ok ? "[flash] done" : "[flash] FAILED");
    return ok;
}
