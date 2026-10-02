# Wireless Controllers

Remote-side firmware for the battle timer's two supported remote protocols.
A remote is one or the other - never both, and nothing else:

- **`ESPNOW_Remote/`** - genuine ESP-NOW (ESP8266, `espnow.h`), each a
  separate PlatformIO project (not variants of one shared source):
  - `controller/` - the Judge remote, 5 buttons.
  - `ready_remote/` - the Red/Blue "driver ready" remote, 1 button. Set
    `DEV_TYPE` to `"RedReady"` or `"BlueReady"` per physical unit before
    flashing. Recovered from git history (commit f13efa9) after a later
    commit overwrote this same filename with an unrelated LLCC68/RadioLib
    design that was never ESP-NOW at all.

  Both received by the timer's `espnowRemotes.cpp`.

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
