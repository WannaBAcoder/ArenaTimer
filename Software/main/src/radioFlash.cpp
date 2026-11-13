#include "radioFlash.h"
#include "config.h"
#include "radio_image.h"
#include <Arduino.h>
#include <string.h>

// ASR6601 ROM bootloader framing (see tremo_loader.py):
//   request : 0xFE, cmd, len16 LE, data[len], crc32 LE, 0xEF
//   response: 0xFE, status, len16 LE, data[len], crc32 LE, 0xEF
// CRC32 is zlib's, taken over everything before the CRC field.
//
// UNTESTED ON HARDWARE as of writing: ported from the TimerRemote copy, which
// is itself a line-for-line port of tremo_loader.py (the verified reference).

#define LOADER_BAUD 921600
#define CMD_SYNC 1
#define CMD_FLASH 3
#define CMD_ERASE 4
#define CMD_VERIFY 5
#define CMD_REBOOT 12
#define CMD_BAUDRATE 16
#define CHUNK 512

// A board with no radio (or without the BOOT/RXD wiring) never answers, and
// this runs on every boot there, so keep the give-up time short.
#define RESET_ATTEMPTS 2
#define SYNCS_PER_ATTEMPT 3
#define SYNC_TIMEOUT_MS 300

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

static void sendRequest(HardwareSerial& ser, uint8_t cmd, const uint8_t* payload, size_t n) {
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
static int waitResponse(HardwareSerial& ser, uint32_t timeoutMs) {
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

static bool command(HardwareSerial& ser, uint8_t cmd, const uint8_t* payload, size_t n,
                    uint32_t timeoutMs) {
    sendRequest(ser, cmd, payload, n);
    return waitResponse(ser, timeoutMs) == 0;
}

// BOOT high across the radio's reset puts the ROM bootloader in charge.
static void resetIntoBootloader() {
    pinMode(RADIO_BOOT_PIN, OUTPUT);
    digitalWrite(RADIO_BOOT_PIN, HIGH);
    pinMode(RADIO_RST_PIN, OUTPUT);
    digitalWrite(RADIO_RST_PIN, LOW);
    delay(100);
    digitalWrite(RADIO_RST_PIN, HIGH);
    delay(100);
    digitalWrite(RADIO_BOOT_PIN, LOW);
}

static void releaseBoot() {
    digitalWrite(RADIO_BOOT_PIN, LOW);
    pinMode(RADIO_BOOT_PIN, INPUT_PULLDOWN); // external pull-down is the real hold; this is a backstop
}

static void beginSerial(HardwareSerial& ser, uint32_t baud) {
    ser.begin(baud, SERIAL_8N1, RADIO_UART_RX, RADIO_UART_TX);
}

// Resets the radio into its ROM bootloader and syncs. Leaves `ser` at
// LOADER_BAUD on success. On failure the caller still has to clean up
// (finishSession).
static bool connectBootloader(HardwareSerial& ser) {
    ser.end();
    beginSerial(ser, LOADER_BAUD);

    bool synced = false;
    for (int attempt = 0; attempt < RESET_ATTEMPTS && !synced; attempt++) {
        resetIntoBootloader();
        for (int i = 0; i < SYNCS_PER_ATTEMPT && !synced; i++) {
            synced = command(ser, CMD_SYNC, nullptr, 0, SYNC_TIMEOUT_MS);
        }
    }
    if (!synced) {
        Serial.println("[flash] bootloader did not answer (no radio, or BOOT/RXD not wired)");
        return false;
    }
    // tremo_loader.py always issues this after connecting (a no-op when the
    // baud is unchanged), so do the same.
    uint8_t arg[4];
    putLE32(arg, LOADER_BAUD);
    if (!command(ser, CMD_BAUDRATE, arg, 4, 1000)) {
        Serial.println("[flash] baud command rejected");
        return false;
    }
    return true;
}

static bool eraseImage(HardwareSerial& ser) {
    uint8_t arg[8];
    putLE32(arg, RADIO_IMAGE_FLASH_ADDR);
    putLE32(arg + 4, RADIO_IMAGE_LEN);
    bool ok = command(ser, CMD_ERASE, arg, 8, 8000);
    if (!ok) Serial.println("[flash] erase failed");
    return ok;
}

// Releases BOOT, optionally tells the bootloader to reboot the radio, and
// puts the port back at the AT baud rate.
static void finishSession(HardwareSerial& ser, bool reboot) {
    if (reboot) {
        uint8_t mode = 0;
        command(ser, CMD_REBOOT, &mode, 1, 1000); // radio resets itself; the reply may not arrive
    }
    releaseBoot();
    ser.end();
    beginSerial(ser, RADIO_BAUD);
}

bool radioEraseFirmware(HardwareSerial& ser) {
    Serial.println("[flash] erasing radio application");
    bool ok = connectBootloader(ser) && eraseImage(ser);
    finishSession(ser, ok);
    Serial.println(ok ? "[flash] erased" : "[flash] erase FAILED / skipped");
    return ok;
}

bool radioFlashFirmware(HardwareSerial& ser) {
    Serial.println("[flash] programming radio");
    uint32_t t0 = millis();

    bool ok = connectBootloader(ser) && eraseImage(ser);

    for (uint32_t off = 0; ok && off < RADIO_IMAGE_LEN; off += CHUNK) {
        uint32_t n = RADIO_IMAGE_LEN - off;
        if (n > CHUNK) n = CHUNK;
        putLE32(payloadBuf, RADIO_IMAGE_FLASH_ADDR + off);
        putLE32(payloadBuf + 4, n);
        memcpy(payloadBuf + 8, &RADIO_IMAGE[off], n);
        ok = command(ser, CMD_FLASH, payloadBuf, 8 + n, 5000);
        if (!ok) Serial.printf("[flash] write failed at %u\n", (unsigned)off);
    }

    if (ok) {
        uint8_t arg[12];
        putLE32(arg, RADIO_IMAGE_FLASH_ADDR);
        putLE32(arg + 4, RADIO_IMAGE_LEN);
        putLE32(arg + 8, RADIO_IMAGE_CRC32);
        ok = command(ser, CMD_VERIFY, arg, 12, 8000);
        if (!ok) Serial.println("[flash] verify failed");
    }

    finishSession(ser, ok);
    Serial.printf("[flash] %s (%u ms)\n", ok ? "done" : "FAILED / skipped", (unsigned)(millis() - t0));
    return ok;
}
