#ifndef LOWPOWER_H
#define LOWPOWER_H

#include <stdint.h>

typedef void (*WakeCallback)();

void lowPowerInit();

// Configures pin as INPUT_PULLUP and arms it as a STOP-mode wake source.
// Call once per button pin during setup().
void lowPowerAttachWakeButton(uint32_t pin, WakeCallback callback);

// Enters STOP mode. Returns once any armed button wakes the MCU.
void lowPowerSleep();

#endif
