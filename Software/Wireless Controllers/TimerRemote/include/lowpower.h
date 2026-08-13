#ifndef LOWPOWER_H
#define LOWPOWER_H

#include <stdint.h>

typedef void (*WakeCallback)();

// STOP-mode sleep is disabled for now: waking from it left the clock
// unstable for long enough that UART bytes sent right after wake (both the
// debug line and the AT command to the radio) came out corrupted, and the
// wake itself added noticeable button-to-packet latency. Low latency matters
// more than battery life at this stage, so the MCU just stays on and polls -
// see git history on this file to restore STOP-mode behavior later.

void lowPowerInit();

// Configures pin as INPUT_PULLUP with a plain GPIO interrupt (no sleep mode
// involved). Call once per button pin during setup().
void lowPowerAttachWakeButton(uint32_t pin, WakeCallback callback);

// No-op placeholder kept so main.cpp's loop() doesn't need restructuring.
void lowPowerSleep();

#endif
