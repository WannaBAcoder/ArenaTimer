#ifndef RF_CONFIG_H
#define RF_CONFIG_H

// Desired RF settings, sent to the radio at every boot via AT+RFCFG (see
// Software/RA08H_p2p_bridge/README.md). The radio itself doesn't persist
// these - it resets to its own compiled-in defaults every power-up - so this
// is the actual persistent config, and it lives here instead of on the
// radio specifically so tuning only needs a USB reflash of the ESP32 rather
// than pulling the radio's wires again.
//
// SF and BW must match whatever the peer radios (the remotes) are
// configured with - see the matching rfConfig.h in
// Software/Wireless Controllers/TimerRemote/include/ - or the two ends
// can't demodulate each other's packets. TX power is one-sided and only
// needs to match this board's own transmit needs, not the peer's.
#define RF_TX_POWER 14 // dBm, -3..22 (SX1262's actual max is 22)
#define RF_SF       7  // spreading factor, 5..12
#define RF_BW       0  // 0 = 125 kHz, 1 = 250 kHz, 2 = 500 kHz
#define RF_CR       1  // coding rate, 1 = 4/5 .. 4 = 4/8

#endif
