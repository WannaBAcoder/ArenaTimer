#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

// Must stay byte-for-byte in sync with
// Software/Wireless Controllers/TimerRemote/include/packet.h - this is the
// LoRa wire format shared between the remotes and this timer. Two separate
// PlatformIO projects (STM32 remote vs ESP32 timer), so it's duplicated
// rather than shared via a common include path.

enum ButtonId : uint8_t {
    BTN_START = 1,
    BTN_PAUSE = 2,
    BTN_RESET = 3,
    BTN_TIME_SEL = 4,
    BTN_BUZZER = 5,
};

enum DeviceRole : uint8_t {
    ROLE_JUDGE = 1,
    ROLE_RED_READY = 2,
    ROLE_BLUE_READY = 3,
};

struct __attribute__((packed)) RemotePacket {
    uint8_t deviceId[4];
    uint8_t role;
    uint8_t buttonId;
};

#endif
