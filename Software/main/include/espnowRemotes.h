#ifndef ESPNOW_REMOTES_H
#define ESPNOW_REMOTES_H

// The original remote-control path (pre-LoRa), kept alongside loraRemotes.h
// so one firmware image serves both the ~5 LoRa-equipped units and the rest
// of the existing ESP-NOW fleet at once - a timer can have Red paired to an
// old ESP-NOW remote and Blue paired to a new LoRa one simultaneously.
// Identical pairing model to loraRemotes.h (receiver-authoritative, one
// MAC per role), just over ESP-NOW instead of a LoRa deviceId.

// Registers the ESP-NOW receive callback. Unlike loraInit(), there's no
// "is the hardware there" probe to fail fast on - ESP-NOW rides the ESP32's
// built-in WiFi radio, which is always present, so this is unconditional.
void espnowInit();

// Loads redMAC/blueMAC/judgeMAC + *EspNowPaired flags from the "bot-timer"
// NVS namespace. Call once from setup(), alongside loadSavedSettings().
void espnowLoadSavedRemotes();

// Wipes only the ESP-NOW side of pairing (NVS + in-memory MACs +
// *EspNowPaired flags). Called by clearRemotes() in main.cpp, which also
// wipes the LoRa side - see the comment on clearRemotes() in config.h.
void espnowClearRemotes();

#endif
