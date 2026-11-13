# TimerRemote - first-board hardware test plan

Order matters: each stage assumes the previous one passed. Tick boxes and
write measured values in the margin. Firmware: `Software/Wireless Controllers/TimerRemote`
(env `remote`, one image for every board). The timer must be running the
matching `Software/main` build to see battery levels.

Test points: TP6 = VSYS, TP7 = +3.3V, TP8 = radio BOOT, TP9 = nPGOOD (LDO enable),
TP11 = SYSOFF. Debug UART (115200, TX only) is J2 pin 5.

Tools: multimeter, bench supply with current limit, a way to read µA in series
with the battery, scope (for TX dips), ST-Link, the Adafruit 350 mAh pack for
first power-up, a second board or the timer for RX.

## 0. Unpowered checks

- [ ] No shorts: VBAT-GND, VSYS-GND, +3.3V-GND, VBUS-GND all read open/high (not near 0 ohm).
- [ ] Battery pack polarity BEFORE plugging in: red wire to J5 pad 1 (VBAT), black to
      pad 2 (GND) - meter continuity from the red lead through the connector to pad 1.
      The board has no reverse protection.
- [ ] Role jumpers RA1 / RB1 fitted as intended for this board (see roleConfig.h:
      RA1 fitted = Judge, RB1 fitted = Blue; open = ready remote / Red).

## 1. Power path with no firmware

(Blank chip: the board only stays up while SYSOFF is held low. Hold the power
button, or clip TP11 to GND, for everything in this section.)

- [ ] Battery connected, nothing held: VSYS (TP6) and +3.3V (TP7) are 0 V.
- [ ] Hold the button: VSYS ~ VBAT, TP7 = 3.3 V (±3%), green LED on. Release: everything off.
- [ ] USB plugged with battery: red LED on while charging; TP7 = 0 V (LDO disabled, TP9 high).
- [ ] Charge current (meter in series with the pack): ~144 mA (R10 = 6.2k, 890/R). Not more than 150 mA.
- [ ] Pack reaches ~4.2 V and the red LED goes out when full (long test - start it early).

## 2. Programming

- [ ] ST-Link on J2, button held (or TP11 clipped). Keep holding until the tool finishes - every
      reset releases the power latch for a few ms.
- [ ] `pio run -t upload` in `Software/Wireless Controllers/TimerRemote` (PowerShell).
- [ ] Open the debug UART on J2 pin 5 at 115200 for the next sections.

## 3. Boot, latch and radio

Power on with the button, release it. Expected debug log, roughly:
`[radio] ... RFCFG AT+RFCFG=22,7,0,1 -> OK`, `RXMODE ... -> OK`, `[radio] init complete`,
`[boot] role=N battery=XXXXmV`, then `[radio] TX ... -> OK` (first battery frame).

- [ ] Board stays on after the button is released (latch works). Green LED on.
- [ ] Blue LED: solid while booting, then one blink = ready. Note boot time: ______ s (expect ~2-3 s).
- [ ] First boot with a radio that does not have the P2P firmware: debug shows
      `[radio] no P2P firmware detected`, `[flash] programming radio`, `[flash] done`, then normal
      boot; LED solid the whole time (this can take 10+ s). Do not remove power during it.
- [ ] If flashing fails: 6 fast blinks and the board powers itself off. Fall back to the manual
      loader in `Software/RA08H_p2p_bridge/README.md` and note what the debug log said.
- [ ] A board that already has the P2P firmware does NOT reflash on later boots.
- [ ] Tap the button for ~20 ms during power-up: it still latches (try a few times).
- [ ] Holding the button through the whole boot: no command is sent when released.

## 4. Role jumpers

Fit each combination in turn and check the `role=` value in the boot line:

- [ ] RA1 fitted: role=1 (Judge), all five buttons active.
- [ ] RA1 open, RB1 open: role=2 (Red ready), only the one button active.
- [ ] RA1 open, RB1 fitted: role=3 (Blue ready).
- [ ] PB7 / PB8 left as analog after sampling (no pull-up current: ~0 V drop across a 1M test resistor to GND).

## 5. Battery measurement and thresholds

Use the bench supply on the battery connector (current limit ~200 mA, USB unplugged).

- [ ] Reported `battery=` vs a multimeter on TP6 at 3.9 V, 3.7 V, 3.5 V: error within ~30 mV.
      Error: ______ mV. (If it is off by a constant, tell me and I will add an offset.)
- [ ] PA4 (C9) reads half of VSYS.
- [ ] 3.55 V: blue LED flashes briefly about once a second. 3.70 V: no flashing. It does not
      flicker around the threshold (stops only above 3.65 V).
- [ ] 3.32 V then drop to 3.28 V: within ~20 s it blinks 3 slow times and powers off. A single
      brief dip below 3.30 V does not trigger it.
- [ ] Power-on attempt at 3.35 V: refuses to boot (3 slow blinks, off).
- [ ] The timer web page shows the voltage and a percentage for this remote after the boot frame, and
      again about a minute later ("heard ago" resets).

## 6. Buttons and sending

Run each against the timer (paired) and watch the debug log (`[radio] TX <hex> -> OK`; the hex is
4-byte device ID + role + button).

- [ ] Judge: START, PAUSE, TIME_SEL trigger the right timer action, sent on press.
- [ ] Judge BUZZER: tone is continuous while held, stops on release (repeats every ~100 ms).
- [ ] Judge RESET and the ready button: a short tap sends on release; holding past 1 s sends nothing.
- [ ] Blue LED flashes briefly (~40 ms) on every button message. With the radio unplugged/unpowered
      you get two extra quick flashes (message refused).
- [ ] Ready remote: ready then tap-out behaviour matches the old remotes.
- [ ] Unpaired remote is ignored by the timer; pairing mode accepts a button press only (a battery
      frame must not pair a remote).

## 7. Power off, idle timeout, watchdog

- [ ] Hold the button: nothing for 1 s, then 3 blinks 1 s apart, then solid LED. Release: board off.
- [ ] Release during the countdown (before the 3rd blink): cancelled, nothing sent, board stays on.
- [ ] After power-off the board is fully off: VSYS and 3.3 V at 0 V.
- [ ] Idle timeout: build with `-D IDLE_POWEROFF_MS=30000UL` - the board blinks twice and powers off
      30 s after the last press; a press inside that window restarts the timer. Normal build is 2 h.
- [ ] Watchdog: build with `-D WDT_SELFTEST` - ~20 s after boot it hangs, resets about 8 s later and
      the boot line says `(after watchdog reset)`. Check the board did NOT power off during the reset
      (the latch pin floats for a few ms); if it does, tell me.
- [ ] Rebuild normally (no test flags) before field use.
- [ ] USB plugged while running: the remote powers off (expected). Unplug: it stays off until the button.

## 8. Current and rail quality

- [ ] Off-state battery current: ______ µA (single-digit to a few tens of µA is fine; investigate >100 µA).
- [ ] Idle (on, asleep) current: ______ mA -> runtime at 350 mAh ~ ______ h (target 10-12 h).
- [ ] TX burst current and 3.3 V rail on a scope at TP7 during a send: dip ______ mV.
      Under ~100 mV is fine; ringing or a deep droop means fit the 1 µF at U5 (see NEXT_REV.md).
- [ ] Battery-level reading is stable (no jumps) right after a transmission.

## 9. Range test (tuned antennas)

The timer logs received packets with RSSI/SNR when built with `ENABLE_DEBUG_LOG`
(`+TEST: RXLRPKT,"<hex>",<rssi>,<snr>`). Radio settings: 22 dBm, SF7, 125 kHz, CR 4/5 on both ends.

Procedure: timer fixed at its normal mounting height; remote held the way it is actually used. At each
distance send 20 presses of the same button, count how many arrive, note typical RSSI/SNR.

| Distance / location | Remote held how | Sent | Received | RSSI | SNR |
|---|---|---|---|---|---|
| 10 m clear | in hand | 20 | | | |
| 25 m clear | in hand | 20 | | | |
| 50 m clear | in hand | 20 | | | |
| venue distance, line of sight | in hand | 20 | | | |
| venue distance, through people / walls | in hand | 20 | | | |
| venue distance | finger ON the BUZZER button | 20 | | | |
| venue distance | on a table, nobody touching | 20 | | | |

- [ ] At the real venue distance at least 19 of 20 arrive, including with a finger on BUZZER.
- [ ] Hold BUZZER for 10 s at the farthest working distance: the tone is continuous, not choppy.
- [ ] Compare a Judge remote and a ready remote at the same spot (different antenna environment).

If the finger-on-BUZZER case loses packets while the others are fine, that is the antenna-detune
case: move or reassign that button (NEXT_REV.md). If everything drops at distance, the next knob is
spreading factor (must change on the timer and every remote together).

## Results

Date / board IDs tested: ______________________   Firmware commit: ______________
Failures and notes:
