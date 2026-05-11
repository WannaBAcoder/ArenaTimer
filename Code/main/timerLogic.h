#ifndef TIMER_LOGIC_H
#define TIMER_LOGIC_H

#include <Arduino.h>
#include "Config.h"
#include "Display.h"

// State-specific functions
void startPreCountdown();
void handlePreCountdownAnimation();
void updateTimer();
void transitionToMatch();
void handlePausedBlink();
void handleClockMode();

// General logic
void processCommand(String cmd);
void setTeamReady(String team);
void checkButtons();

#endif