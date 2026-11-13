#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

// Must stay byte-for-byte in sync with
// Software/Wireless Controllers/TimerRemote/include/packet.h - this is the
// LoRa wire format shared between the remotes and this timer. Two separate
// PlatformIO projects (STM32 remote vs ESP32 timer), so it's duplicated
// rather than shared via a common include path.

// Button IDs match the old ESP-NOW struct_message.buttonID values used by
// Software/main's OnDataRecv, so the eventual LoRa receiver firmware can
// reuse the same downstream command mapping.
enum ButtonId : uint8_t {
    BTN_START = 1,
    BTN_PAUSE = 2,
    BTN_RESET = 3,
    BTN_TIME_SEL = 4,
    BTN_BUZZER = 5,
    // Not a button: marks a RemoteStatusPacket (see below).
    BTN_STATUS = 0x80,
};

// Replaces the old ESP-NOW "deviceType" string (Judge/RedReady/BlueReady) -
// a single byte instead of a 15-char string to keep LoRa airtime down.
enum DeviceRole : uint8_t {
    ROLE_JUDGE = 1,
    ROLE_RED_READY = 2,
    ROLE_BLUE_READY = 3,
};

// Deliberately compact: every extra byte costs LoRa airtime.
// deviceId is a short hash of the STM32's 96-bit factory UID (see pairing.h)
// standing in for what the old ESP-NOW scheme got for free from the sender's
// MAC address.
//
// This 6-byte button packet is UNCHANGED from the original format, so any
// timer firmware (old or new) handles button presses from these remotes.
struct __attribute__((packed)) RemotePacket {
    uint8_t deviceId[4];
    uint8_t role;
    uint8_t buttonId;
};

// Battery/status frame: a separate 7-byte packet, sent at boot and as a
// periodic heartbeat. Kept apart from RemotePacket on purpose - the timer's
// decoder rejects any frame whose length isn't exactly 6 bytes, so timers
// that predate this frame simply ignore it instead of misreading it, and
// button presses stay 6 bytes (shortest airtime, fully compatible).
//   buttonId : always BTN_STATUS
//   battery  : (millivolts - 2500) / 10, clamped to 0..255 (2.50..5.05 V)
struct __attribute__((packed)) RemoteStatusPacket {
    uint8_t deviceId[4];
    uint8_t role;
    uint8_t buttonId;
    uint8_t battery;
};

static inline uint8_t batteryMvToCode(uint16_t mv) {
    if (mv <= 2500) return 0;
    uint16_t c = (mv - 2500) / 10;
    return c > 255 ? 255 : (uint8_t)c;
}
static inline uint16_t batteryCodeToMv(uint8_t code) { return 2500 + (uint16_t)code * 10; }

#endif
