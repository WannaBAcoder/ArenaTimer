#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// Button GPIOs (STM32G030K6T6, see PCB Files/TimerRemote netlist).
// All buttons are 1:1 GPIO-to-GND, active low, need INPUT_PULLUP.
#define PIN_START PB1
#define PIN_PAUSE PA5
#define PIN_RESET PA6 // also the ReadyRemote's single button
#define PIN_TSEL PA7
#define PIN_BUZZ PB0

// RA-08H control
#define PIN_RADIO_RST PA1
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
