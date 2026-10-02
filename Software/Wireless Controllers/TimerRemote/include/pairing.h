#ifndef PAIRING_H
#define PAIRING_H

#include <stdint.h>

// Derives a 4-byte device ID from the STM32's 96-bit factory-programmed UID.
// Stands in for the sender identity ESP-NOW got for free from the source
// MAC address - LoRa has no equivalent, so this is what the timer will use
// to recognize "this remote" during pairing.
void getDeviceId(uint8_t out[4]);

#endif
