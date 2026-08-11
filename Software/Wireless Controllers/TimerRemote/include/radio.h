#ifndef RADIO_H
#define RADIO_H

#include <stdint.h>
#include <stddef.h>

// RA-08H is UART-only on this board (no SPI/SWD to the module itself) - see
// PCB Files/TimerRemote netlist. Everything below talks AT commands over
// that UART. radioSendPacket()'s implementation is a placeholder against the
// AT+TEST=TXLRPKT convention seen on other ASR6601-based modules; it is
// UNCONFIRMED for RA-08H specifically until verified against real hardware
// with radioDiagnose() (see task: "Build RA-08H AT diagnostic boot mode").

void radioInit();

// Sends a handful of candidate AT commands (LoRaWAN baseline sanity check,
// plus the AT+TEST / AT+CTX P2P-mode guesses) and prints raw responses out
// PIN_DEBUG_TX so they can be read with a USB-UART adapter on J2. Call this
// once when bringing up the first unit to find out whether stock firmware
// already supports P2P mode, before deciding whether reflashing is needed.
void radioDiagnose();

// Sends one AT command line (CRLF appended automatically) and captures the
// raw response into respBuf (null-terminated, truncated to bufLen-1) within
// timeoutMs. Returns the number of bytes captured.
size_t radioSendATCommand(const char* cmd, char* respBuf, size_t bufLen, uint32_t timeoutMs);

// Transmits one RemotePacket-sized payload over LoRa. Placeholder pending
// radioDiagnose() results - see radio.cpp.
bool radioSendPacket(const uint8_t* data, size_t len);

#endif
