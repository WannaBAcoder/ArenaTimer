#ifndef RADIO_FLASH_H
#define RADIO_FLASH_H

#include <HardwareSerial.h>

// Flashes the embedded P2P bridge image (radio_image.h) into the RA-08H over
// its UART using the ASR6601 ROM bootloader - same protocol as
// Software/RA08H_p2p_bridge/tremo_loader.py, and the same code as the
// TimerRemote's radioFlash.cpp, ported to the ESP32.
//
// Sequence: hold the radio's BOOT pin HIGH (GPIO RADIO_BOOT_PIN), pulse its
// reset, sync with the bootloader at 921600 baud, erase, write in 512 byte
// chunks, verify the CRC32, reboot the radio into the new image. Takes a few
// seconds. Leaves `ser` back at the normal AT baud rate with BOOT released.
//
// Hardware needed on the ESP32_Lora board (not present on the original
// boards): RA-08H pin 15 (BOOT) wired to RADIO_BOOT_PIN with a pull-down to
// GND, and RA-08H pin 12 (RXD) tied to pin 10 so the bootloader hears TX.
// On boards without those the bootloader never answers and this just returns
// false after a short timeout.
//
// Returns true if the image was written and verified. Does not check that the
// new firmware answers AT commands - the caller does that.
bool radioFlashFirmware(HardwareSerial& ser);

// Erases the radio's application image (leaves it blank, so it stops answering
// AT commands until reflashed) and reboots it. Used by the RADIO_FLASH_TEST
// build to prove the bootloader link; not part of normal operation.
bool radioEraseFirmware(HardwareSerial& ser);

#endif
