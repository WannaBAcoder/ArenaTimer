#ifndef RCE_MANAGER_H
#define RCE_MANAGER_H

#include <Arduino.h>

// RCE Manager integration (BotCodeBashers/rce-manager-integrations): this
// timer joins an arena's live connection as a hardware device, mirrors the
// clock RCE Manager publishes, and reports local start/pause/reset/finish.
//
// "RCE mode" is simply "an integration key and an arena are configured".
// With neither set, none of this runs and the timer behaves exactly as it
// always has. In RCE mode the things that only make sense standalone are
// switched off: the 3-2-1 pre-countdown, tapouts, require-driver-ready,
// the 2/3-minute switch and web time entry (the match length now comes from
// RCE Manager), and multi-timer IP sync (every timer in an arena subscribes
// to RCE Manager directly instead).

enum RceEvent {
    RCE_EV_START,   // local start - clock is running as of now
    RCE_EV_PAUSE,   // local pause
    RCE_EV_RESET,   // local reset to idle
    RCE_EV_FINISH   // local clock reached zero
};

// True while rceApplyExternal() is applying a state received from RCE
// Manager, so the resulting local transition is not echoed back as a command.
extern volatile bool rceApplying;

void rceLoadConfig();                    // call once at boot, after NVS is usable
void rceLoop();                          // call every loop() iteration
bool rceModeActive();                    // key + arena configured

// Returns false (and leaves config untouched) if arena or device are invalid.
// key may be empty to keep the stored one.
bool rceSetConfig(const String& host, uint16_t port, bool tls, const String& key,
                  const String& arena, const String& device);
void rceClearConfig();

String rceStatusJson();                  // {"mode":..,"status":..,...} (never includes the key)

// Report a local clock event to RCE Manager (no-op outside RCE mode, while
// applying a remote state, or when offline - the physical timer never waits
// on the cloud).
void rceNotifyLocal(RceEvent ev);

// Implemented in timerLogic.cpp: force the local state machine to a state
// published by RCE Manager. state: IDLE, RUNNING, PAUSED or FINISHED.
void rceApplyExternal(int state, int remainingSeconds, int durationSeconds);
unsigned long timerRemainingMs();        // ms left on the local clock

#endif
