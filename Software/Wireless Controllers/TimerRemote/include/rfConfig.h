#ifndef RF_CONFIG_H
#define RF_CONFIG_H

// Desired RF settings, sent to the radio at every boot via AT+RFCFG (see
// Software/RA08H_p2p_bridge/README.md). The radio itself doesn't persist
// these - it resets to its own compiled-in defaults every power-up - so this
// is the actual persistent config, and it lives here instead of on the
// radio specifically so tuning only needs an ST-Link reflash (fast, no
// BOOT-jumper dance) rather than pulling the radio's wires again.
//
// SF and BW must match whatever the peer radio (the other remotes, and the
// timer) is configured with, or the two ends can't demodulate each other's
// packets. TX power is one-sided and only needs to match your own transmit
// needs, not the peer's.
#define RF_TX_POWER 22 // dBm, -3..22 (22 is the SX1262's actual max)
#define RF_SF       7  // spreading factor, 5..12
#define RF_BW       0  // 0 = 125 kHz, 1 = 250 kHz, 2 = 500 kHz
#define RF_CR       1  // coding rate, 1 = 4/5 .. 4 = 4/8

// A remote only ever transmits (button presses) - it never needs to
// receive - so RF_RX_MODE = 0 (TX-only) lets the radio sleep between sends
// instead of idling in its most power-hungry state (continuous RX) for no
// functional benefit. Left at 1 (continuous, matching the radio's own
// compiled-in default - i.e. no behavior change) until that's actually been
// measured and validated on hardware; flip to 0 once confirmed worthwhile.
#define RF_RX_MODE 1 // 0 = TX-only (sleep between sends), 1 = continuous RX

#endif
