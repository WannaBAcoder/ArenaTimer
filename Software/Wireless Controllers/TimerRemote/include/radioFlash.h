#ifndef RADIO_FLASH_H
#define RADIO_FLASH_H

// Flashes the embedded P2P bridge image (radio_image.h) into an RA-08H over
// its UART using the ASR6601 ROM bootloader - the same protocol as
// Software/RA08H_p2p_bridge/tremo_loader.py, ported to run on the STM32.
//
// Sequence: hold the radio's BOOT pad HIGH, pulse its reset, sync with the
// bootloader at 921600 baud, erase, write in 512 byte chunks, verify the
// CRC32, reboot the radio into the new image. Takes several seconds.
// Leaves radioPort() back at the normal AT baud rate with BOOT released.
//
// Returns true if the image was written and verified. Does not check that
// the new firmware then answers AT commands - the caller does that.
bool radioFlashFirmware();

#endif
