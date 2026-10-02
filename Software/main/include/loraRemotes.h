#ifndef LORA_REMOTES_H
#define LORA_REMOTES_H

// Replaces the old ESP-NOW OnDataRecv path. Pairing is receiver-authoritative
// (each timer keeps its own paired deviceId per role, same as the old MAC
// allow-list) so the same set of remotes can be paired to multiple timers at
// once and control them all in parallel - nothing on the remote side needs
// to know or care how many timers are listening.

// Probes for the RA-08H with the existing boot-time retry/timeout, then
// returns immediately (skipping the rest of radio setup) if nothing ever
// answers, rather than spending that same retry budget twice. This is what
// lets one firmware image serve both LoRa-equipped and ESP-NOW-only
// hardware: boards without the radio installed just don't pay for it past
// this one probe. See loraRadioPresent() below.
void loraInit();

// Call every loop() iteration. Non-blocking: drains whatever's arrived on
// the RA-08H UART without stalling the rest of the timer's event loop. A
// no-op if loraInit() never detected a radio.
void loraPoll();

// True once loraInit() has run and gotten a response from the radio. Lets
// callers (e.g. /status) report whether this unit actually has LoRa
// hardware, rather than just whether the firmware supports it.
bool loraRadioPresent();

// Loads redID/blueID/judgeID + *LoraPaired flags from the "bot-timer" NVS
// namespace. Call once from setup(), alongside loadSavedSettings().
void loraLoadSavedRemotes();

// Wipes only the LoRa side of pairing (NVS + in-memory IDs + *LoraPaired
// flags). Called by clearRemotes() in main.cpp, which also wipes the
// ESP-NOW side - see the comment on clearRemotes() in config.h.
void loraClearRemotes();

#endif
