#ifndef LORA_REMOTES_H
#define LORA_REMOTES_H

// Replaces the old ESP-NOW OnDataRecv path. Pairing is receiver-authoritative
// (each timer keeps its own paired deviceId per role, same as the old MAC
// allow-list) so the same set of remotes can be paired to multiple timers at
// once and control them all in parallel - nothing on the remote side needs
// to know or care how many timers are listening.

void loraInit();

// Call every loop() iteration. Non-blocking: drains whatever's arrived on
// the RA-08H UART without stalling the rest of the timer's event loop.
void loraPoll();

// Loads redID/blueID/judgeID + *Paired flags from the "bot-timer" NVS
// namespace. Call once from setup(), alongside loadSavedSettings().
void loraLoadSavedRemotes();

// clearRemotes() itself declared in config.h (called from the existing
// /clear_remotes web handler) - defined in loraRemotes.cpp.

#endif
