# Wireless Controllers

Remote-side firmware for the battle timer's two supported remote protocols.
A remote is one or the other - never both, and nothing else:

- **`ESPNOW_Remote/`** - genuine ESP-NOW (ESP8266, `espnow.h`). One shared
  source (`controller.ino`) builds the Judge remote and both Red/Blue
  "driver ready" remotes - each physical unit is just flashed with its own
  `DEV_TYPE`/`PINS`/`BUTTON_IDS` constants at the top of the file. Received
  by the timer's `espnowRemotes.cpp`.

- **`TimerRemote/`** - the correct, current LoRa remote (STM32 + RA-08H
  radio module, talks to the timer's own RA-08H over actual LoRa RF, not
  WiFi). Received by the timer's `loraRemotes.cpp`. This is the one to
  build on for any new LoRa remote work.

- **`_archive/`** - superseded prototypes, kept for reference only. Not
  wired up to anything current and safe to delete outright once nobody
  needs to look back at them:
  - `timer_wifi_logic/` - an early draft of the *timer's* own ESP-NOW
    receive logic, predating and superseded by `Software/main`'s
    `espnowRemotes.cpp`. Not a remote.
