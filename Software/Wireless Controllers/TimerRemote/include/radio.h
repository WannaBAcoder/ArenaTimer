#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stddef.h>
#include <HardwareSerial.h>

// RA-08H is UART-only on this board (no SPI/SWD to the module itself) - see
// PCB Files/TimerRemote netlist. Everything below talks AT commands over
// that UART at 9600 baud (LPUART hardware limit).
//
// This targets the custom P2P firmware in Software/RA08H_p2p_bridge, NOT the
// stock Ai-Thinker image - stock is LoRaWAN-only and has no device-to-device
// path. A module that doesn't answer like the P2P firmware is flashed from
// the copy embedded in this firmware (radioFlash.h) before use.
//   AT+TEST=TXLRPKT,"<HEX>"  ->  OK, then the packet goes out over LoRa.

// Resets the radio, makes sure it is running the P2P firmware (flashing it
// if not), then pushes the RF config. Returns true only when the radio is
// up and configured. Blocks for up to a few seconds (much longer if it has
// to flash the radio).
bool radioInit();

// Sends one AT command line (CRLF appended automatically) and captures the
// raw response into respBuf (null-terminated, truncated to bufLen-1) within
// timeoutMs. Returns the number of bytes captured.
size_t radioSendATCommand(const char* cmd, char* respBuf, size_t bufLen, uint32_t timeoutMs);

// Transmits one payload over LoRa (hex-encoded into the AT command). Returns
// true only if the module acknowledged with OK.
bool radioSendPacket(const uint8_t* data, size_t len);

// The UART the radio sits on, shared with radioFlash.cpp (which temporarily
// re-opens it at the ROM bootloader's baud rate).
HardwareSerial& radioPort();

// Debug line on J2 pin 5.
HardwareSerial& debugPort();

#endif
