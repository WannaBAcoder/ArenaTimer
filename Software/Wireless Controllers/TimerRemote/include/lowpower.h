#ifndef LOWPOWER_H
#define LOWPOWER_H

// Idle power strategy: SLEEP mode (CPU halted with WFI, clocks and
// peripherals left running), woken by the 1kHz SysTick or any interrupt, then
// loop() polls the buttons. This replaced the earlier STOP-mode attempt:
// STOP stops the PLL, and the clock was not stable again for long enough
// after wake that UART bytes sent right away (debug line and the AT command
// to the radio) came out corrupted, and the wake added button-to-packet
// latency. Plain SLEEP has none of that - the radio command goes out the
// instant a press is seen - at the cost of a few mA instead of a few uA for
// the MCU, which is small against the radio and LDO quiescent current and
// well inside the battery budget (350mAh, 10-12h target).
//
// If the MCU's share ever matters, STOP mode with an explicit clock restore
// is the next step - see git history of this file.

// Configures a button pin as INPUT_PULLUP. Buttons are polled, not
// interrupt-driven.
void lowPowerInitButton(unsigned int pin);

// Halts the CPU until the next interrupt (at most ~1ms with SysTick running).
void lowPowerSleep();

#endif
