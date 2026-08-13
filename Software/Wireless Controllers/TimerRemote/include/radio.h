#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stddef.h>

// RA-08H is UART-only on this board (no SPI/SWD to the module itself) - see
// PCB Files/TimerRemote netlist. Everything below talks AT commands over
// that UART at 9600 baud (LPUART hardware limit).
//
// This targets the custom P2P firmware in Software/RA08H_p2p_bridge, NOT the
// stock Ai-Thinker image - stock is LoRaWAN-only and has no device-to-device
// path, so every module must be reflashed. Protocol confirmed on hardware:
//   AT+TEST=TXLRPKT,"<HEX>"  ->  OK, then the packet goes out over LoRa.

void radioInit();

// Sends one AT command line (CRLF appended automatically) and captures the
// raw response into respBuf (null-terminated, truncated to bufLen-1) within
// timeoutMs. Returns the number of bytes captured.
size_t radioSendATCommand(const char* cmd, char* respBuf, size_t bufLen, uint32_t timeoutMs);

// Transmits one RemotePacket-sized payload over LoRa. Returns true only if
// the module acknowledged with OK.
bool radioSendPacket(const uint8_t* data, size_t len);

#endif
