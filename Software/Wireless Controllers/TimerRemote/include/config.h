#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Button GPIOs (STM32G030K8T6, see PCB Files/TimerRemote netlist).
// All buttons are 1:1 GPIO-to-GND, active low, need INPUT_PULLUP.
#define PIN_START PB1
#define PIN_PAUSE PA5
// PA6 is the board's power button. The Judge's RESET (SW3) and the ready
// remote's only button (SW6) both land on this net, and either one also
// pulls the BQ24075's SYSOFF low through D1 to power the board on. It is
// therefore a dual-use button: tap = command, long hold = power off.
#define PIN_RESET PA6
#define PIN_TSEL PA7
#define PIN_BUZZ PB0

// Power latch (Q3 gate, 100k pull-down R15). HIGH holds SYSOFF low = board
// stays on after the power button is released. LOW releases it; the board
// then loses power as soon as the button is up (and USB is absent).
#define PIN_PWR_EN PA15

// Blue status LED (D11). Its anode is on VSYS and its cathode goes through
// R18 (470R) to this pin, so it is ACTIVE LOW: drive LOW = on, release the
// pin (INPUT) = off.
#define PIN_STATUS_LED PA8

// Battery sense: VSYS through R21/R22 (1M/1M, so the pin sees VSYS / 2) with
// C9 (100nF) on the node. VSYS tracks VBAT within ~tens of mV while running
// from battery.
#define PIN_BATT_SENSE PA4
#define BATT_DIVIDER_RATIO 2

// Role/variant solder jumpers RA1 / RB1: 0R to GND when fitted, otherwise
// the pin reads HIGH with the internal pull-up. Sampled once at boot, then
// returned to analog mode so they cost nothing while asleep. See
// roleConfig.h for which state means what.
#define PIN_ROLE_JUDGE PB7 // RA1
#define PIN_ROLE_COLOR PB8 // RB1

// RA-08H control
#define PIN_RADIO_RST PA1
// RA-08H BOOT/IO2 (R20 10k pulls it low). Driven HIGH by the STM32 only
// while it resets the radio into its ROM bootloader to (re)flash it.
#define PIN_RADIO_BOOT PA3
// Hard limit: the module's AT interface runs on its LPUART, which cannot
// exceed 9600 baud. Must match the RA08H_p2p_bridge firmware.
#define RADIO_BAUD 9600
// UART to RA-08H is USART1: PA9 TX (-> module LPRXD), PA10 RX (<- module TXD).
//
// IMPORTANT: this package has two physical pads that can each carry PA9 (and
// likewise for PA10) - the plain, default-location pad, and a second,
// "remapped" pad selectable via the chip's pin-remap feature. Per the actual
// PCB netlist (PCB Files/TimerRemote), the radio is wired to the REMAPPED
// pads, not the default ones - the default PA9/PA10 pads go nowhere (net
// "unconnected-(U2-NC/PA9-Pad19)" etc). Arduino's plain PA9/PA10 macros
// resolve to the default (unconnected) pads, so using them here compiles and
// runs but talks to a floating pin - the radio never receives anything and
// the STM32 never sees a reply, with no error either side. PA_9_R/PA_10_R
// (defined in the STM32duino variant's PinNamesVar.h) are the remap-selected
// PinName values that actually land on the wired pads; HardwareSerial's
// PinName-typed constructor overload is required to use them (the plain
// uint32_t Arduino-pin-number overload can't address a remapped pin).
#define PIN_RADIO_TX PA_9_R
#define PIN_RADIO_RX PA_10_R

// Debug-only, one-way TX broken out to J2 pin 5 (no RX wired)
#define PIN_DEBUG_TX PA2

#endif
