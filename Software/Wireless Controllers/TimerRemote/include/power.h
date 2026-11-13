#ifndef POWER_H
#define POWER_H

#include <stdint.h>

// ---- Battery thresholds (millivolts of VSYS, which tracks VBAT) -----------
// The AP2112K needs ~100-250mV of headroom over 3.3V, so below ~3.4V the 3.3V
// rail itself starts to sag and the radio's 22dBm bursts get unreliable.
// Readings are taken at idle (never right after a transmission), so these are
// resting voltages.
#define BATT_MIN_BOOT_MV 3400     // refuse to start below this
#define BATT_LOW_WARN_MV 3600     // status LED starts flashing (~20% left)
#define BATT_LOW_CLEAR_MV 3650    // ...and stops again above this (hysteresis)
#define BATT_SHUTDOWN_MV 3300     // software shutoff: power off below this
#define BATT_SHUTDOWN_READINGS 2  // consecutive low readings required (rejects a transient sag)
#define BATT_CHECK_MS 10000UL     // how often the battery is measured while running

// Latches the board on (PA15 HIGH). Call as early as possible after reset -
// the power button only holds the board up while it is physically pressed.
void powerLatchOn();

// Releases the latch. The board really loses power once the power button is
// also up (and USB absent), so this blocks forever; call it only when the
// device is meant to turn off.
[[noreturn]] void powerOff();

// Status LED (active low, see config.h). ledOff() releases the pin entirely.
void ledOn();
void ledOff();
// Blinks the LED `count` times (on onMs, off offMs), ending with it off.
void ledBlink(uint8_t count, uint16_t onMs, uint16_t offMs);

// Battery voltage in millivolts: averaged ADC reading of the VSYS divider,
// scaled by the real analog supply measured from VREFINT (so it stays
// accurate even when the 3.3V rail sags with the battery).
uint16_t batteryReadMv();

// True while the power/RESET/ready button is held down.
bool powerButtonDown();

// ---- Watchdog ---------------------------------------------------------------
// Independent watchdog (own 32kHz oscillator, cannot be stopped once started).
// If anything hangs for WDT_TIMEOUT_MS - a wedged radio UART, a stuck loop -
// the chip resets and runs the normal boot again. Every wait in the firmware
// goes through waitMs() or calls wdtFeed(), so it only fires on a real hang.
#define WDT_TIMEOUT_MS 8000

void wdtStart();
void wdtFeed();
// True if the last reset was caused by the watchdog (and clears the flag).
bool wdtCausedReset();
// delay() that keeps feeding the watchdog.
void waitMs(uint32_t ms);

#endif
