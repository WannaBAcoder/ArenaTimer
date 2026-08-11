#include "pairing.h"
#include <Arduino.h>

// Folds the 96-bit UID (3x 32-bit words) down to 4 bytes with a simple
// XOR-fold. This only needs to be "distinct enough" across the handful of
// remotes in this fleet, not cryptographically unique.
void getDeviceId(uint8_t out[4]) {
    uint32_t w0 = HAL_GetUIDw0();
    uint32_t w1 = HAL_GetUIDw1();
    uint32_t w2 = HAL_GetUIDw2();
    uint32_t folded = w0 ^ w1 ^ w2;

    out[0] = (folded >> 24) & 0xFF;
    out[1] = (folded >> 16) & 0xFF;
    out[2] = (folded >> 8) & 0xFF;
    out[3] = folded & 0xFF;
}
