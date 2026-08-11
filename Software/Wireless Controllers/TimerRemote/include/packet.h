#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

// Button IDs match the old ESP-NOW struct_message.buttonID values used by
// Software/main's OnDataRecv, so the eventual LoRa receiver firmware can
// reuse the same downstream command mapping.
enum ButtonId : uint8_t {
    BTN_START = 1,
    BTN_PAUSE = 2,
    BTN_RESET = 3,
    BTN_TIME_SEL = 4,
    BTN_BUZZER = 5,
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
struct __attribute__((packed)) RemotePacket {
    uint8_t deviceId[4];
    uint8_t role;
    uint8_t buttonId;
};

#endif
