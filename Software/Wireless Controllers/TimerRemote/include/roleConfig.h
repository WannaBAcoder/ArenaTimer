#ifndef ROLE_CONFIG_H
#define ROLE_CONFIG_H

// Role is chosen per board by two 0R solder jumpers to GND (RA1, RB1),
// sampled at boot with the internal pull-ups. Fitted = pin reads LOW.
// One firmware image serves every board; nothing here needs editing per unit.
//
//   RA1 (PB7)  fitted -> Judge controller (5 buttons: START/PAUSE/RESET/TIME_SEL/BUZZER)
//              open   -> ready/tap-out remote ("driver", single button)
//   RB1 (PB8)  only read on a ready remote:
//              fitted -> Blue
//              open   -> Red
//
// Flip either of these if the jumper convention should be the other way
// around (e.g. to make the more numerous variant the unpopulated default).
#define JUDGE_WHEN_RA1_FITTED 1 // 1: fitted = Judge, open = ready remote; 0: the reverse
#define BLUE_WHEN_RB1_FITTED 1  // 1: fitted = Blue,  open = Red;          0: the reverse

#endif
