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

#endif
