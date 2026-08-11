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
// UART to RA-08H is USART1: PA9 TX (-> module LPRXD), PA10 RX (<- module TXD)
#define PIN_RADIO_TX PA9
#define PIN_RADIO_RX PA10

// Debug-only, one-way TX broken out to J2 pin 5 (no RX wired)
#define PIN_DEBUG_TX PA2

#endif
