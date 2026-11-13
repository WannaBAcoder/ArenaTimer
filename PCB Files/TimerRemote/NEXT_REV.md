# TimerRemote - next board revision

Running list of changes to make the next time the board is opened. Nothing
here is needed for the current batch (ordered 2026-11-12).

## Programming header (J2)

J2 is a 1x6 header: VBAT, NRST, SWDIO, SWCLK, TX, GND. The board only powers
up when SYSOFF is pulled low, so programming currently needs the power button
held (or a clip lead from TP11 to GND) for the whole session. USB does not
help: it disables the 3.3 V regulator via nPGOOD.

Add one of:
- A 7th pin wired to SYSOFF, with a jumper to GND while programming. The
  board then runs from the battery. Do not feed the ST-Link's 3.3 V into VBAT
  while a battery is attached.
- A pin feeding the ST-Link's 3.3 V through a small Schottky onto the +3.3V
  rail, so the board powers from the ST-Link alone (no battery, no button).
  The diode stops back-feeding the ST-Link when the battery is also
  connected. Check the ST-Link's current limit against radio TX bursts.

## Run while charging (optional behaviour)

Today the 3.3 V rail is off whenever USB is plugged in: U5 pin 3 (EN) is wired
directly to the BQ24075's nPGOOD, which goes low when USB is good. No FET is
involved (Q4 only forces SYSOFF low on USB; Q3 is the firmware power latch), so
omitting a FET does not change this.

To let a remote run while plugged in (including with a dead battery, via the
charger's power path): give U5 EN its own net with two 0402 0R footprints, one
to nPGOOD (current behaviour) and one to VSYS; fit one per board. VSYS is
regulated to 5.5 V on USB (BQ24075 VOUT(REG)); the AP2112K takes up to 6 V.
Charge indication is unaffected (D10 lives on VSYS + nCHARGE, not on the 3.3 V
rail).

For the current batch, the equivalent bodge is lifting U5 pin 3 and wiring it
to VSYS (TP6).

Firmware work if this is adopted (not done): there is no USB sense pin, but a
battery never exceeds 4.2 V while USB gives ~5.5 V, so VSYS above ~4.4 V can
mean "on USB". While on USB: skip the battery-level frame / low-battery flash /
low-battery shutoff (report "charging" to the timer), and note that
hold-to-power-off cannot turn the board off while plugged in.

## Smaller items

- J5 has no reverse-polarity protection; JST PH battery polarity is not
  standardised between vendors (see the battery pack notes). Consider a
  keyed/protected input.
- ERC: add PWR_FLAG on GND and VBUS (the two power_pin_not_driven errors);
  refresh the RA-08H symbol (lib_symbol_mismatch warning).
- Cosmetic: name the PA4 net `VSYS_SENSE` instead of `Net-(U2-PA4)`.
- If the BUZZER button detunes the antenna on the bench, move it or reassign
  button roles (decided: firmware-side workaround only if it shows up).
- AP2112K output cap (U5): none fitted, datasheet says 1 uF is enough and
  ~20 uF sits on the rail. Add a 1 uF at U5 only if the 3.3 V rail rings
  during a radio TX burst.
