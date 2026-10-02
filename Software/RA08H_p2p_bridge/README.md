# RA-08H P2P Bridge Firmware

Replacement firmware for the RA-08H (ASR6601) LoRa modules on the ArenaTimer
boards. The stock Ai-Thinker firmware is **LoRaWAN-only** (`release/v1.3.4
LoRaWAN for US915`) and has no device-to-device path — LoRaWAN requires a
gateway and network server, so it can't be used for remote-to-timer links.
This firmware replaces it with raw LoRa P2P.

It exposes the same text protocol the host code already speaks, so
`TimerRemote/src/radio.cpp` and `Software/main/src/loraRemotes.cpp` need no
changes:

```
host -> radio:  AT+TEST=TXLRPKT,"<HEX>"\n
radio -> host:  OK                                     (accepted, transmitting)
                ERROR                                  (bad command or payload)
                +TEST: RXLRPKT,"<HEX>",<rssi>,<snr>    (packet received)
```

Identical on both boards — which side is "remote" vs "timer" is purely a
host-MCU concern.

## Runtime RF config

TX power, spreading factor, bandwidth, and coding rate are adjustable at
runtime, so range/power tuning doesn't require reflashing the radio — only
the host MCU (much cheaper to reflash via SWD, no BOOT-jumper dance needed):

```
host -> radio:  AT+RFCFG=<power>,<sf>,<bw>,<cr>\n
radio -> host:  OK                                (applied)
                ERROR                              (out of range or malformed)

host -> radio:  AT+RFCFG?\n
radio -> host:  +RFCFG: <power>,<sf>,<bw>,<cr>
                OK
```

| field | range | meaning |
|---|---|---|
| power | -3 to 22 | dBm (SX1262's actual max is 22, not the 14 the defaults use) |
| sf | 5 to 12 | spreading factor |
| bw | 0, 1, 2 | 0 = 125 kHz, 1 = 250 kHz, 2 = 500 kHz |
| cr | 1 to 4 | coding rate, 1 = 4/5 ... 4 = 4/8 |

**Not persisted** — RFCFG changes are RAM-only and reset to the `DEFAULT_*`
values in `main.c` on every power-up/reset. The host is expected to re-send
its desired config (from its own persistent settings) after every boot; see
`TimerRemote/src/radio.cpp`'s `radioInit()` and `Software/main/src/loraRemotes.cpp`'s
`loraInit()` for where that happens.

Spreading factor and bandwidth must match on both ends of a link — the two
radios can't demodulate each other's packets otherwise. TX power is
one-sided and only needs to change on the transmitting radio.

## Runtime RX mode

Also adjustable at runtime: whether the radio sits in continuous receive or
sleeps between transmits. A LoRa remote in this system only ever sends
button presses and never needs to receive, so leaving it in continuous RX —
the SX1262's most power-hungry state — burns battery for no functional
benefit. The timer needs the opposite: it must always be listening.

```
host -> radio:  AT+RXMODE=<mode>\n
radio -> host:  OK                                (applied)
                ERROR                              (out of range or malformed)

host -> radio:  AT+RXMODE?\n
radio -> host:  +RXMODE: <mode>
                OK
```

| mode | meaning |
|---|---|
| 0 | TX-only — radio sleeps between sends, wakes automatically to transmit |
| 1 | continuous RX (default) — always listening, as before this command existed |

Same as RFCFG: not persisted, RAM-only, reset to `DEFAULT_RX_MODE` on every
power-up. Waking from sleep to transmit needs no special handling — any SPI
transaction wakes the SX1262 automatically (a documented hardware feature),
and this firmware already relies on that exact transition after every
completed TX regardless of RX mode.

Both host projects' `rfConfig.h` currently default to `RF_RX_MODE = 1`
(continuous, matching pre-RXMODE behavior) even on remotes, until TX-only
mode has been measured and validated on real hardware. Flip to `0` on a
remote's `rfConfig.h` once confirmed worthwhile, and reflash just that
board's STM32/ESP32 — no radio reflash needed, same firmware image runs
either mode.

## Radio configuration (power-on defaults)

| setting | value |
|---|---|
| frequency | 915 MHz (US915) |
| spreading factor | SF7 |
| bandwidth | 125 kHz |
| coding rate | 4/5 |
| TX power | 14 dBm |
| preamble | 8 symbols |
| max payload | 32 bytes |

## UART wiring

Two different UART peripherals are used, matching how the stock firmware did
it and matching the board's existing traces:

- **LPUART RX** — GPIOD pin 12 (`LPRXD`/IO60), commands arrive from the host MCU
- **UART0 TX** — GPIOB pin 1 (`TXD`/IO17), responses go out; shared with the
  flash/debug port

Both at **9600 baud**. This is a hardware limit, not a preference: the AT
interface runs on the LPUART, whose baud rate cannot exceed 9600.

## Building

Needs the Ai-Thinker SDK (`Ai-Thinker-Open/Ai-Thinker-LoRaWAN-Ra-08`). Drop
this directory in as `projects/ASR6601CB-EVAL/examples/lora/p2p_bridge`, then
from Git Bash:

```bash
export TREMO_SDK_PATH=/path/to/Ai-Thinker-LoRaWAN-Ra-08
export PATH="$PATH:$HOME/.platformio/packages/toolchain-gccarmnoneeabi/bin:/c/ProgramData/chocolatey/bin"
make
```

No WSL required — PlatformIO's bundled `toolchain-gccarmnoneeabi` (xPack GNU
Arm Embedded GCC) plus chocolatey's `make` build it natively on Windows, which
is what the SDK's own `envsetup.sh` falls back to when `arm-none-eabi-gcc` is
already on PATH.

A prebuilt `p2p_bridge.bin` is included.

## Flashing

The RA-08H enters download mode when **BOOT (IO2, module pin 15) is high at
reset**. On these boards that pad is tied to GND by the JP1 solder jumper, so:

1. Cut the JP1 bridge, wire that pad to VCC (needs a pullup — a bare floating
   wire is not enough).
2. Power-cycle the board.
3. Flash — the adapter's RX taps the existing `TXD` trace, no bodge needed:

```bash
python $TREMO_SDK_PATH/build/scripts/tremo_loader.py -p COM16 -b 921600 flash 0x08000000 p2p_bridge.bin
```

4. Return BOOT to GND and power-cycle again for normal boot.

Future board revisions should provision a proper programming header — doing
this by hand does not scale past a handful of boards.

## Debugging note

**Start the serial listener _before_ power-cycling the board.** This firmware
prints its boot log once and then goes quiet, so a listener attached a few
seconds later sees nothing and looks identical to a dead board. That false
signal cost a long debugging detour; `uart_probe`-style firmware that prints
in a loop is not affected, which makes the contrast misleading.

Expected boot output:

```
=== p2p_bridge boot ===
CKPT: xo32k enabled
... (per-step init checkpoints) ...
CKPT: rf config applied (power=14 sf=7 bw=0 cr=1)
READY
```

`READY` means the radio is initialized and listening.

## Verified

Bench test, two boards a few inches apart, 3/3 packets received intact at
RSSI −5 dBm / SNR 13 dB.
