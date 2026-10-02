#include "espnowRemotes.h"
#include "config.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>

// Wire format for the existing ESP-NOW remotes - must stay byte-for-byte in
// sync with whatever those remotes' own firmware sends. Unlike packet.h's
// RemotePacket (LoRa), this predates this project's LoRa work and nothing
// here should change it.
typedef struct __attribute__((packed)) {
    char deviceType[15];
    int buttonID;
} espnow_message;

static uint8_t redMAC[6] = {0};
static uint8_t blueMAC[6] = {0};
static uint8_t judgeMAC[6] = {0};

static void saveRole(const uint8_t *mac, const char* role) {
    preferences.begin("bot-timer", false);

    if (strcmp(role, "RedReady") == 0) {
        memcpy(redMAC, mac, 6);
        preferences.putBytes("redMAC", mac, 6);
        redEspNowPaired = true;
    } else if (strcmp(role, "BlueReady") == 0) {
        memcpy(blueMAC, mac, 6);
        preferences.putBytes("blueMAC", mac, 6);
        blueEspNowPaired = true;
    } else if (strcmp(role, "Judge") == 0) {
        memcpy(judgeMAC, mac, 6);
        preferences.putBytes("judgeMAC", mac, 6);
        judgeEspNowPaired = true;
    }

    preferences.end();
    recomputePairedFlags();
    Serial.printf("[BIND] %s paired successfully (ESP-NOW).\n", role);
}

static void OnDataRecv(const esp_now_recv_info *info, const uint8_t *data, int len) {
    // Not every sender's struct_message is __attribute__((packed)) like
    // ours - the Red/Blue ready-remote's copy isn't, so its compiler inserts
    // a padding byte before buttonID (deviceType[15] isn't 4-byte aligned),
    // making its packets 20 bytes instead of our 19. A strict length *match*
    // silently dropped every one of its packets before deviceType was even
    // looked at - exactly why pairing only ever worked for Judge (whose
    // remote's struct happens to already be packed). deviceType itself always
    // lands correctly regardless (padding comes after it), which is all
    // Red/Blue role handling actually reads - only Judge's buttonID switch
    // needs its sender's layout to match ours, and it already does.
    espnow_message incoming;
    if (len < (int)sizeof(incoming)) return;
    memcpy(&incoming, data, sizeof(incoming));
    const uint8_t* mac = info->src_addr;

    // If it's a buzzer hold packet, bypass the cooldown so a held button
    // keeps tracking as a stream rather than getting rate-limited mid-hold.
    static unsigned long lastPacketTime = 0;
    if (incoming.buttonID != 5) {
        if (millis() - lastPacketTime < 300) return;
        lastPacketTime = millis();
        DEBUG_LOG("[ESP-NOW] Role: %s | Button: %d\n", incoming.deviceType, incoming.buttonID);
    }

    if (pairingMode) {
        saveRole(mac, incoming.deviceType);
        pairingMode = false;
        return;
    }

    if (strcmp(incoming.deviceType, "RedReady") == 0) {
        if (redEspNowPaired && memcmp(mac, redMAC, 6) == 0) {
            if (currentState == RUNNING) {
                queueCommand("tapoutRed");
            } else {
                queueCommand("readyRed");
            }
        } else {
            Serial.println("[ESP-NOW REJECT] Unpaired or unauthorized Red packet source.");
        }
    }
    else if (strcmp(incoming.deviceType, "BlueReady") == 0) {
        if (blueEspNowPaired && memcmp(mac, blueMAC, 6) == 0) {
            if (currentState == RUNNING) {
                queueCommand("tapoutBlue");
            } else {
                queueCommand("readyBlue");
            }
        } else {
            Serial.println("[ESP-NOW REJECT] Unpaired or unauthorized Blue packet source.");
        }
    }
    else if (strcmp(incoming.deviceType, "Judge") == 0) {
        if (judgeEspNowPaired && memcmp(mac, judgeMAC, 6) == 0) {
            switch (incoming.buttonID) {
                case 1: queueCommand("start");  break;
                case 2: queueCommand("pause");  break;
                case 3: queueCommand("reset");  break;
                case 4: queueCommand("switch"); break;
                case 5:
                    if (audioEnabled && remoteAudioEnabled) {
                        triggerBeep(250);
                    }
                    break;
            }
        } else {
            Serial.println("[ESP-NOW REJECT] Unpaired or unauthorized Judge packet source.");
        }
    }
}

void espnowInit() {
    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Error initializing - remotes on this path won't respond.");
        return;
    }
    esp_now_register_recv_cb(OnDataRecv);

    // WiFi modem sleep (the default power-save mode) can silently miss an
    // ESP-NOW packet that arrives while the radio's asleep between beacon
    // wake windows - no error, no retry, just an occasional button press
    // that doesn't land and needs a second try. Disabling it trades a bit
    // of idle power draw (irrelevant on mains/battery-pack timer hardware)
    // for not losing those.
    esp_wifi_set_ps(WIFI_PS_NONE);
}

void espnowLoadSavedRemotes() {
    preferences.begin("bot-timer", true);
    redEspNowPaired = (preferences.getBytes("redMAC", redMAC, 6) == 6);
    blueEspNowPaired = (preferences.getBytes("blueMAC", blueMAC, 6) == 6);
    judgeEspNowPaired = (preferences.getBytes("judgeMAC", judgeMAC, 6) == 6);
    preferences.end();
    recomputePairedFlags();
}

void espnowClearRemotes() {
    preferences.begin("bot-timer", false);
    preferences.remove("redMAC");
    preferences.remove("blueMAC");
    preferences.remove("judgeMAC");
    preferences.end();

    redEspNowPaired = false;
    blueEspNowPaired = false;
    judgeEspNowPaired = false;

    memset(redMAC, 0, 6);
    memset(blueMAC, 0, 6);
    memset(judgeMAC, 0, 6);
}
