#ifndef BROWSER_H
#define BROWSER_H

const char* html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>ESP32 Battle Timer</title>
        <style>
            body { font-family: Arial, sans-serif; text-align: center; background-color: black; color: white; padding: 10px; }
            #countdown { font-size: 64px; color: white; margin: 10px 0; }
            button { font-size: 18px; margin: 5px; padding: 12px 20px; cursor: pointer; border-radius: 5px; border: none; transition: 0.3s; }
            .small-btn { font-size: 14px; padding: 8px 15px; }
            .flex-row { display: flex; justify-content: center; gap: 10px; margin: 10px 0; }
            /* Centered label+value pair, not stretched edge to edge - keeps
               a long value like "PAIRED (LoRa+ESP-NOW)" from wrapping into
               the next role's line the way inline pipe-separated text did,
               without the awkward gap space-between left across the box. */
            .status-row { display: flex; justify-content: center; gap: 8px; margin: 4px 0; }
            .status-row span:first-child { min-width: 70px; text-align: right; }
            /* Grid instead of independently-centered rows: a fixed label
               width broke down once values varied a lot in length (e.g.
               "OPEN" vs "PAIRED (LoRa+ESP-NOW)") - each row centered itself
               around a different total width, so "Red"/"Blue"/"Judge"
               never shared a common left edge. Grid columns auto-size to
               the widest label/value in that column, and justify-content
               centers the whole two-column block as one unit, so labels
               line up and the gap adapts instead of either being hardcoded
               or stretched edge to edge. */
            .status-grid { display: grid; grid-template-columns: auto auto; justify-content: center; column-gap: 10px; row-gap: 4px; margin: 8px 0; }
            .status-grid > *:nth-child(odd) { text-align: right; }
            .status-grid > *:nth-child(even) { text-align: left; }
            .status { margin: 15px auto; font-size: 16px; border: 1px solid #444; padding: 10px; border-radius: 8px; max-width: 400px; }
            #pairingBanner { display: none; background: #0000ff; padding: 15px; margin: 10px; font-weight: bold; border-radius: 5px; }
            input[type="number"], input[type="text"] { padding: 8px; width: 50px; text-align: center; border-radius: 4px; border: 1px solid #444; }
            input[name="ssid"], input[name="pass"] { width: 150px; text-align: left; }
            label { font-size: 16px; margin: 10px; display: block; }
            
            /* Visual lockout styles */
            .disabled-ui { opacity: 0.3; pointer-events: none; filter: grayscale(1); }
            .blocked-feature { opacity: 0.4; pointer-events: none; cursor: not-allowed; filter: grayscale(1); }
            
            input[type="range"] { width: 80%; margin-top: 10px; }
            input[type="color"] { vertical-align: middle; cursor: pointer; }

            /* Below this width, #settingsGrid stays an unstyled wrapper and
               every .status box keeps stacking full-width like before -
               same single-column layout the page always had on mobile. */
            @media (min-width: 700px) {
                #settingsGrid {
                    display: grid;
                    grid-template-columns: 1fr 1fr;
                    gap: 15px;
                    align-items: stretch;
                    max-width: 850px;
                    margin: 0 auto;
                }
                /* Stretch (the grid default we're restating explicitly)
                   makes every box in a row match the row's tallest box,
                   instead of each hugging its own content height - that
                   mismatch was what made the grid look ragged. */
                #settingsGrid .status { max-width: none; margin: 0; }
            }
        </style>
    </head>
    <body>
        <h1 id="timerTitle">Battle Timer</h1>

        <div style="margin-bottom: 15px;">
            <input id="timerNameInput" placeholder="Timer name (e.g. Mat 1)" maxlength="24" autocapitalize="none" autocorrect="off" style="padding:5px; margin:5px; width:150px; text-align:center;">
            <button id="nameSaveBtn" class="small-btn" onclick="applyTimerName()" style="background:gray; color:white;">Save</button>
        </div>

        <div id="pairingBanner">PAIRING MODE ACTIVE... (<span id="pairingCountdown">--</span>s)</div>

        <div id="settingsGrid">
        <div id="timerControlsSection" class="status">
            <p id="countdown">02:00</p>

            <div id="readyStatusRows" class="flex-row" style="display:none; margin-bottom:10px;">
                <span>Red: <strong id="redReadyStat" style="color:red;">NOT READY</strong></span>
                <span>Blue: <strong id="blueReadyStat" style="color:red;">NOT READY</strong></span>
            </div>

            <div id="timerControls">
                <div id="manualTimeSection" style="margin-bottom: 20px;">
                    <input type="number" id="manualMin" min="0" max="60"
                        oninput="this.value = !!this.value && Math.abs(this.value) >= 0 ? Math.min(Math.abs(this.value), 60) : null"
                        placeholder="MM"> :
                    <input type="number" id="manualSec" min="0" max="59"
                        oninput="this.value = !!this.value && Math.abs(this.value) >= 0 ? Math.min(Math.abs(this.value), 59) : null"
                        placeholder="SS">
                    <button id="setTimeBtn" class="small-btn" onclick="applyTime()" style="background:green; color:white;">Set Time</button>
                    <button id="setMatchTimeBtn" class="small-btn" onclick="applyMatchTime()" style="background:#1565c0; color:white;">Match Time</button>
                    <div style="font-size:0.8em; margin-top:4px; color:#aaa;">Set Time: just this match. Match Time: Reset returns to this until reboot (Switch 2/3m still overrides it).</div>
                </div>

                <div>
                    <button id="startBtn" onclick="controlTimer('start')" style="background:green; color:white;">Start</button>
                    <button id="pauseBtn" onclick="controlTimer('pause')" style="background:orange;">Pause</button>
                    <button id="resetBtn" onclick="controlTimer('reset')" style="background:#8b0000; color:white;">Reset</button>
                    <button id="switchBtn" onclick="toggleTime()" style="background:blue; color:white;">Switch 2/3m</button>
                </div>
            </div>
        </div>

        <div id="wifiSection" class="status">
            <h3>WiFi Settings</h3>
            <div>Current SSID: <strong id="currentSSID">----</strong></div>
            <form action="/setwifi" method="POST" style="margin-top:10px;">
                <input id="wifiSSID" name="ssid" placeholder="SSID" required autocapitalize="none" autocorrect="off" style="padding:5px; margin:5px;"><br>
                <input id="wifiPass" name="pass" type="text" placeholder="Password" required autocapitalize="none" autocorrect="off" style="padding:5px; margin:5px;"><br>
                <button id="wifiBtn" type="submit" class="small-btn" style="background:gray; color:white;">Save & Reboot</button>
                <button id="wifiWipeBtn" type="button" class="small-btn" onclick="wipeWifi()" style="background:red; color:white;">Wipe</button>
            </form>
            <div class="flex-row" style="margin-top:15px; border-top: 1px solid #444; padding-top: 10px; flex-wrap: wrap;">
                <a href="/update"><button id="updateBtn" class="small-btn" style="background:#444; color:white;">Firmware Update</button></a>
                <button id="factoryResetBtn" class="small-btn" onclick="factoryReset()" style="background:#8b0000; color:white;">Restore Factory Settings</button>
            </div>
        </div>

        <div id="systemStatusSection" class="status">
            <h3>System Status</h3>
            <div class="status-grid">
                <span>Red</span><span id="redStat" style="color:red;">OPEN</span>
                <span>Blue</span><span id="blueStat" style="color:red;">OPEN</span>
                <span>Judge</span><span id="judgeStat" style="color:red;">OPEN</span>
            </div>
            <div style="text-align:center; font-size:0.9em; margin-top:4px;">LoRa radio: <strong id="loraRadioStat">----</strong></div>

            <div id="pairingControls" class="flex-row" style="margin-top:10px;">
                <button id="pairBtn" class="small-btn" onclick="startPairing()" style="background:orange;">Pair Remotes</button>
                <button id="wipeBtn" class="small-btn" onclick="wipeRemotes()" style="background:red; color:white;">Wipe All</button>
            </div>
            
            <div id="readySection">
                <input type="checkbox" id="readyToggle" onchange="toggleReady()">
                <label for="readyToggle" style="display:inline;">Require Driver Ready</label>
            </div>

            <div id="tapoutSection" style="margin-top: 10px;">
                <input type="checkbox" id="tapoutToggle" onchange="toggleTapoutAllow()">
                <label for="tapoutToggle" style="display:inline;">Enable Tapout</label>
            </div>
        </div>

        <div id="displaySection" class="status">
            <!-- Only displaySettingsContent dims during a match/clock mode,
                 not this whole box - clockSection sits outside it as a
                 sibling, deliberately left alone, since CLOCK_MODE is one of
                 the lockout states and its own toggle has to stay usable to
                 escape clock mode. opacity/filter can't be un-done on a
                 descendant once an ancestor has them, so the only way to
                 dim "everything but this one control" is to never put that
                 control under the dimmed element in the first place. -->
            <div id="displaySettingsContent">
                <h3>Display Settings</h3>
                <div>
                    <label>Digit Color: <input type="color" id="colorPicker" onchange="applyColor()" value="#ff0000"></label>
                </div>
                <div style="margin-top:15px;">
                    <label>Brightness: <br>
                    <input type="range" id="brightSlider" min="10" max="230" onchange="applyBrightness()" value="127"></label>
                </div>
                <div style="margin-top:15px; border-top: 1px solid #444; padding-top: 10px;">
                    <input type="checkbox" id="flipToggle" onchange="toggleFlip()">
                    <label for="flipToggle" style="display:inline;">Flip Display (Upside Down)</label>
                </div>
            </div>
            <div id="clockSection" style="margin-top: 15px; border-top: 1px solid #444; padding-top: 10px;">
                <input type="checkbox" id="clockToggle" onchange="toggleClockMode()">
                <label for="clockToggle" style="display:inline;">Enable Clock Mode</label>
            </div>
        </div>

        <div id="audioSection" class="status">
            <h3>Audio Alerts</h3>
            <div style="margin-bottom: 10px;">
                <input type="checkbox" id="audioToggle" onchange="applyAudioSettings()"> 
                <label for="audioToggle" style="display:inline;">Enable Global Audio</label>
            </div>
            <div style="margin-bottom: 10px;">
                <input type="checkbox" id="remoteAudioToggle" onchange="applyAudioSettings()"> 
                <label for="remoteAudioToggle" style="display:inline;">Enable Remote Audio</label>
            </div>
            <div style="margin-top:15px; border-top: 1px solid #444; padding-top: 10px;">
                <label style="display:inline; margin-right: 15px;">
                    <input type="radio" name="outputSelect" value="0" onchange="applyAudioSettings()"> Buzzer (Tone)
                </label>
                <label style="display:inline;">
                    <input type="radio" name="outputSelect" value="1" onchange="applyAudioSettings()"> Relay Pin
                </label>
            </div>
        </div>

        <div id="syncSection" class="status">
            <h3>Multi-Timer Sync</h3>
            <div>My IP: <strong id="myIP">----</strong></div>
            <div style="margin-top:10px;">
                <input id="syncIpInput" placeholder="Sync target IP" autocapitalize="none" autocorrect="off" style="padding:5px; margin:5px; width:120px;">
                <button id="syncSaveBtn" class="small-btn" onclick="applySyncTarget()" style="background:gray; color:white;">Sync</button>
                <button id="syncClearBtn" class="small-btn" onclick="clearSyncTarget()" style="background:red; color:white;">Clear</button>
            </div>
            <div style="margin-top:5px; font-size:0.9em;">Currently syncing to: <strong id="syncTargetStat">None</strong></div>
        </div>

        </div>

        <script>
            let isLockingUI = false;
            let lastKnownState = "IDLE";
            let webSocket;
            let preCountdownInterval = null;

            // The /status poll below (setInterval) already recovers on its
            // own after a dropped connection - a failed fetch() there just
            // gets silently skipped and the next tick tries again. This
            // socket doesn't: a WebSocket that closes stays closed forever
            // unless something opens a new one, and it's the only source
            // for the live per-second countdown during RUNNING/
            // PRE_COUNTDOWN_LOOP (the poll explicitly leaves the countdown
            // alone in those two states). Without this, a WiFi drop during
            // a running match would freeze the countdown number specifically
            // until someone manually reloaded the page.
            function connectWebSocket() {
                webSocket = new WebSocket(`ws://${window.location.hostname}:81/`);

                webSocket.onmessage = function(event) {
                    document.getElementById('countdown').textContent = event.data;
                };

                webSocket.onclose = function() {
                    setTimeout(connectWebSocket, 2000);
                };
            }
            connectWebSocket();

            window.onload = function() {
                fetch('/status')
                .then(r => r.json())
                .then(data => {
                    if(data.brightness !== undefined) document.getElementById('brightSlider').value = data.brightness;
                    if(data.digitColor) document.getElementById('colorPicker').value = "#" + data.digitColor;
                    if(data.displayInverted !== undefined) document.getElementById('flipToggle').checked = data.displayInverted;
                    if(data.readyRequired !== undefined) document.getElementById('readyToggle').checked = data.readyRequired;
                    if(data.tapoutEnabled !== undefined) document.getElementById('tapoutToggle').checked = data.tapoutEnabled;
                    
                    if(data.audioEnabled !== undefined) document.getElementById('audioToggle').checked = data.audioEnabled;
                    if(data.remoteAudioEnabled !== undefined) document.getElementById('remoteAudioToggle').checked = data.remoteAudioEnabled;
                    if(data.audioOutput !== undefined) {
                        let radioBtn = document.querySelector(`input[name="outputSelect"][value="${data.audioOutput}"]`);
                        if(radioBtn) radioBtn.checked = true;
                    }
                    
                    if(data.myIP) document.getElementById('myIP').textContent = data.myIP;
                    if(data.wifiSSID !== undefined) document.getElementById('currentSSID').textContent = data.wifiSSID || '(none)';
                    if(data.timerName !== undefined) setTimerName(data.timerName);
                    if(data.syncTargetIP !== undefined) {
                        document.getElementById('syncTargetStat').textContent = data.syncTargetIP || 'None';
                        document.getElementById('syncIpInput').value = data.syncTargetIP;
                    }

                    const isClockMode = (data.state === "CLOCK_MODE");
                    document.getElementById('clockToggle').checked = isClockMode;
                    updateControls(isClockMode);
                });
            };

            function applyColor() {
                const hex = document.getElementById('colorPicker').value.replace('#', '');
                fetch(`/setcolor?hex=${hex}`);
            }

            function applyBrightness() {
                const val = document.getElementById('brightSlider').value;
                fetch(`/setbrightness?val=${val}`);
            }

            function toggleFlip() { fetch('/flip'); }

            function setTimerName(name) {
                document.getElementById('timerTitle').textContent = name || 'Battle Timer';
                document.title = name ? name + ' - Battle Timer' : 'ESP32 Battle Timer';
                if (document.activeElement !== document.getElementById('timerNameInput')) {
                    document.getElementById('timerNameInput').value = name || '';
                }
            }
            function applyTimerName() {
                const name = document.getElementById('timerNameInput').value.trim();
                fetch(`/setname?name=${encodeURIComponent(name)}`).then(() => setTimerName(name));
            }

            function applyAudioSettings() {
                const enabled = document.getElementById('audioToggle').checked;
                const remoteEnabled = document.getElementById('remoteAudioToggle').checked;
                const checkedRadio = document.querySelector('input[name="outputSelect"]:checked');
                const output = checkedRadio ? checkedRadio.value : 0;
                fetch(`/setaudio?enabled=${enabled}&remoteEnabled=${remoteEnabled}&output=${output}`);
            }

            function toggleClockMode() {
                const clockToggle = document.getElementById('clockToggle');
                const timerActive = (lastKnownState === "RUNNING" || lastKnownState === "PAUSED" || lastKnownState === "PRE_COUNTDOWN_LOOP");
                if (clockToggle.checked && timerActive) { clockToggle.checked = false; return; }
                isLockingUI = true; 
                if (clockToggle.checked) {
                    const now = new Date();
                    fetch(`/synctime?h=${now.getHours()}&m=${now.getMinutes()}&s=${now.getSeconds()}`);
                    updateControls(true);
                } else {
                    fetch('/control?cmd=clockOff');
                    updateControls(false);
                }
                setTimeout(() => { isLockingUI = false; }, 1500);
            }

            function updateControls(isLocked) {
                const timerControls = document.getElementById('timerControls');
                if (isLocked) timerControls.classList.add('disabled-ui');
                else timerControls.classList.remove('disabled-ui');
            }

            function controlTimer(action) { fetch(`/control?cmd=${action}`); }
            function toggleTime() { fetch('/control?cmd=switch'); }
            function toggleReady() { fetch(`/control?cmd=readytoggle&state=${document.getElementById('readyToggle').checked ? "on" : "off"}`); }
            function toggleTapoutAllow() { fetch(`/control?cmd=tapouttoggle&state=${document.getElementById('tapoutToggle').checked ? "on" : "off"}`); }
            // Unlike a slider/color-picker/checkbox, whose own visible state
            // already reflects what was just set with no round trip needed,
            // "Currently syncing to" is a separate readout that nothing else
            // refreshes - and Clear needs to empty the input box itself, or
            // it looks like it did nothing.
            function refreshSyncStatus() {
                fetch('/status')
                    .then(r => r.json())
                    .then(data => {
                        document.getElementById('syncTargetStat').textContent = data.syncTargetIP || 'None';
                        document.getElementById('syncIpInput').value = data.syncTargetIP || '';
                    });
            }
            function applySyncTarget() {
                const ip = document.getElementById('syncIpInput').value.trim();
                fetch(`/setsyncip?ip=${ip}`).then(refreshSyncStatus);
            }
            function clearSyncTarget() {
                fetch('/setsyncip?ip=').then(refreshSyncStatus);
            }
            function startPairing() { fetch('/pair'); }
            function wipeRemotes() { if(confirm("Wipe all remotes?")) fetch('/clear_remotes'); }
            function wipeWifi() { if(confirm("Clear saved WiFi credentials and reboot into setup mode?")) fetch('/clearwifi'); }
            function factoryReset() { if(confirm("Wipe ALL saved settings - remotes, WiFi, sync target, colors, brightness, audio, everything - and reboot? This cannot be undone.")) fetch('/factoryreset'); }
            function applyTime() { fetch(`/settime?m=${document.getElementById('manualMin').value || 0}&s=${document.getElementById('manualSec').value || 0}`); }
            function applyMatchTime() { fetch(`/setmatchtime?m=${document.getElementById('manualMin').value || 0}&s=${document.getElementById('manualSec').value || 0}`); }
            
            setInterval(() => {
                fetch('/status')
                    .then(r => r.json())
                    .then(data => {
                        const enteringPreCountdown = data.state === "PRE_COUNTDOWN_LOOP" && lastKnownState !== "PRE_COUNTDOWN_LOOP";
                        lastKnownState = data.state;

                        if (enteringPreCountdown && data.preCountdown !== undefined) {
                            // Only 3 ticks spread over 3 seconds, so a poll
                            // landing slightly out of phase was immediately
                            // obvious - seed once from the server, then run
                            // the actual tick locally on the browser's own
                            // clock instead of waiting on the next poll.
                            let localCount = data.preCountdown;
                            document.getElementById('countdown').textContent = localCount;
                            if (preCountdownInterval !== null) clearInterval(preCountdownInterval);
                            preCountdownInterval = setInterval(() => {
                                localCount--;
                                if (localCount > 0) {
                                    document.getElementById('countdown').textContent = localCount;
                                } else {
                                    clearInterval(preCountdownInterval);
                                    preCountdownInterval = null;
                                }
                            }, 1000);
                        } else if (data.state !== "PRE_COUNTDOWN_LOOP" && data.state !== "RUNNING" && data.currentTime) {
                            if (preCountdownInterval !== null) {
                                clearInterval(preCountdownInterval);
                                preCountdownInterval = null;
                            }
                            document.getElementById('countdown').textContent = data.currentTime;
                        }

                        document.getElementById('pairingBanner').style.display = data.pairing ? 'block' : 'none';
                        if (data.pairing && data.pairingSecondsLeft !== undefined) {
                            document.getElementById('pairingCountdown').textContent = data.pairingSecondsLeft;
                        }
                        updateStatus('redStat', data.red, data.redLora, data.redEspNow);
                        updateStatus('blueStat', data.blue, data.blueLora, data.blueEspNow);
                        updateStatus('judgeStat', data.judge, data.judgeLora, data.judgeEspNow);
                        if (data.loraRadioPresent !== undefined) {
                            const el = document.getElementById('loraRadioStat');
                            el.textContent = data.loraRadioPresent ? 'Detected' : 'Not detected (ESP-NOW only)';
                            el.style.color = data.loraRadioPresent ? '#4caf50' : '#888';
                        }
                        document.getElementById('readyStatusRows').style.display = data.readyRequired ? 'flex' : 'none';
                        if (data.readyRequired) {
                            const redEl = document.getElementById('redReadyStat');
                            redEl.textContent = data.redReady ? 'READY' : 'NOT READY';
                            redEl.style.color = data.redReady ? 'green' : 'red';
                            const blueEl = document.getElementById('blueReadyStat');
                            blueEl.textContent = data.blueReady ? 'READY' : 'NOT READY';
                            blueEl.style.color = data.blueReady ? 'green' : 'red';
                        }

                        // Keep display/audio/ready/tapout/sync controls live
                        // even when this page didn't originate the change -
                        // e.g. a setting that arrived here via another
                        // timer's sync push, or was edited from a second
                        // open tab. Skip color/brightness while focused so a
                        // poll tick can't fight an in-progress drag.
                        if (document.activeElement !== document.getElementById('colorPicker') && data.digitColor) {
                            document.getElementById('colorPicker').value = "#" + data.digitColor;
                        }
                        if (document.activeElement !== document.getElementById('brightSlider') && data.brightness !== undefined) {
                            document.getElementById('brightSlider').value = data.brightness;
                        }
                        if (data.displayInverted !== undefined) document.getElementById('flipToggle').checked = data.displayInverted;
                        if (data.readyRequired !== undefined) document.getElementById('readyToggle').checked = data.readyRequired;
                        if (data.tapoutEnabled !== undefined) document.getElementById('tapoutToggle').checked = data.tapoutEnabled;
                        if (data.audioEnabled !== undefined) document.getElementById('audioToggle').checked = data.audioEnabled;
                        if (data.remoteAudioEnabled !== undefined) document.getElementById('remoteAudioToggle').checked = data.remoteAudioEnabled;
                        if (data.audioOutput !== undefined) {
                            const radioBtn = document.querySelector(`input[name="outputSelect"][value="${data.audioOutput}"]`);
                            if (radioBtn) radioBtn.checked = true;
                        }
                        if (data.syncTargetIP !== undefined && document.activeElement !== document.getElementById('syncIpInput')) {
                            document.getElementById('syncTargetStat').textContent = data.syncTargetIP || 'None';
                            document.getElementById('syncIpInput').value = data.syncTargetIP;
                        }
                        if (data.timerName !== undefined) setTimerName(data.timerName);

                        const isRunning = (data.state === "RUNNING" || data.state === "PRE_COUNTDOWN_LOOP");
                        const isPaused = (data.state === "PAUSED");
                        const isTapout = (data.state === "TAPOUT");
                        const isClockMode = (data.state === "CLOCK_MODE");
                        const isIdle = (data.state === "IDLE" || data.state === "FINISHED");

                        const timerControls = document.getElementById('timerControls');
                        if (isTapout || isPaused || isIdle) {
                            timerControls.classList.remove('disabled-ui');
                        } else if (isClockMode) {
                            timerControls.classList.add('disabled-ui');
                        }

                        const countdownEl = document.getElementById('countdown');
                        if (isTapout) {
                            countdownEl.style.color = data.tapoutBlue ? '#0011ff' : '#ff3333';
                        } else if (isPaused) {
                            countdownEl.style.color = '#ffcc00';
                        } else if (isClockMode) {
                            // This display never reflects clock mode at all - the
                            // physical LED digits show the actual wall-clock time
                            // separately, so this just sits on whatever match time
                            // was last active. Dimming it signals "not live right
                            // now" instead of looking like a current, correct time.
                            countdownEl.style.color = '#555';
                        } else {
                            countdownEl.style.color = 'white';
                        }

                        // 1. TIMER BUTTON LOCKOUT
                        const lockGroup = ['startBtn', 'resetBtn', 'switchBtn', 'setTimeBtn', 'setMatchTimeBtn', 'pauseBtn'];
                        lockGroup.forEach(id => {
                            const btn = document.getElementById(id);
                            if (!btn) return;

                            if (isClockMode) {
                                btn.classList.add('blocked-feature');
                                btn.disabled = true;
                            } else if (isRunning) {
                                if (id === 'pauseBtn') {
                                    btn.classList.remove('blocked-feature');
                                    btn.disabled = false;
                                } else {
                                    btn.classList.add('blocked-feature');
                                    btn.disabled = true;
                                }
                            } else if (isPaused || isTapout) {
                                if (id === 'resetBtn' || (id === 'startBtn' && isPaused)) {
                                    btn.classList.remove('blocked-feature');
                                    btn.disabled = false;
                                } else {
                                    btn.classList.add('blocked-feature');
                                    btn.disabled = true;
                                }
                            } else {
                                if (id === 'pauseBtn') {
                                    btn.classList.add('blocked-feature');
                                    btn.disabled = true;
                                } else {
                                    btn.classList.remove('blocked-feature');
                                    btn.disabled = false;
                                }
                            }
                        });

                        // Independent of match state: processCommand() already
                        // silently refuses "start" when Require Driver Ready is on
                        // and someone isn't ready yet - gray the button out to match
                        // instead of only finding that out by pressing it.
                        if (data.readyRequired && (!data.redReady || !data.blueReady)) {
                            const startBtn = document.getElementById('startBtn');
                            if (startBtn) {
                                startBtn.classList.add('blocked-feature');
                                startBtn.disabled = true;
                            }
                        }

                        // 2. SETTINGS PANEL LOCKOUT
                        const shouldLockSettings = isRunning || isPaused || isTapout || isClockMode;
                        // displaySettingsContent (not the whole displaySection box) so Clock
                        // Mode's own toggle - a sibling outside it, see the HTML comment next
                        // to #clockSection - stays fully visible and clickable even while
                        // everything else dims, including during CLOCK_MODE itself where it's
                        // the only way back out. opacity/grayscale can't be undone on a
                        // descendant once an ancestor has them, so the only way to spare one
                        // control is to keep it outside the dimmed element entirely.
                        const sections = ['wifiSection', 'manualTimeSection', 'audioSection', 'syncSection', 'systemStatusSection', 'displaySettingsContent'];
                        sections.forEach(id => {
                            const el = document.getElementById(id);
                            if (el) {
                                if (shouldLockSettings) el.classList.add('blocked-feature');
                                else el.classList.remove('blocked-feature');
                            }
                        });

                        // clockSection gets the narrower isRunning||isPaused||isTapout
                        // condition, not full shouldLockSettings - it's functionally
                        // disabled during a match the same way (clockToggle.disabled
                        // below already covers that), but visually looked exactly like
                        // every other still-active control, which read as "you could
                        // tap this mid-match" even though the click silently wouldn't
                        // do anything. Still excluded from dimming during CLOCK_MODE
                        // itself, same as always - that's the one state it has to stay
                        // visibly usable in, to escape clock mode.
                        const clockSectionEl = document.getElementById('clockSection');
                        if (clockSectionEl) {
                            if (isRunning || isPaused || isTapout) clockSectionEl.classList.add('blocked-feature');
                            else clockSectionEl.classList.remove('blocked-feature');
                        }

                        // 3. INDIVIDUAL INPUT COMPONENT DISABLING
                        const inputs = ['pairBtn', 'wipeBtn', 'wifiSSID', 'wifiPass', 'wifiBtn', 'wifiWipeBtn', 'updateBtn', 'factoryResetBtn', 'clockToggle', 'readyToggle', 'tapoutToggle', 'colorPicker', 'brightSlider', 'flipToggle', 'audioToggle', 'remoteAudioToggle', 'syncIpInput', 'syncSaveBtn', 'syncClearBtn', 'timerNameInput', 'nameSaveBtn'];
                        inputs.forEach(id => {
                            const el = document.getElementById(id);
                            if (el) {
                                if (id === 'clockToggle') {
                                    // Clock toggle stays unlocked during CLOCK_MODE so we can escape!
                                    el.disabled = isRunning || isPaused || isTapout;
                                } else if (id === 'tapoutToggle') {
                                    el.disabled = isClockMode; 
                                } else {
                                    el.disabled = shouldLockSettings;
                                }
                            }
                        });

                        const radioGroup = document.querySelectorAll('input[name="outputSelect"]');
                        radioGroup.forEach(radio => { radio.disabled = shouldLockSettings; });

                        if (!isLockingUI) {
                            document.getElementById('clockToggle').checked = isClockMode;
                            updateControls(isClockMode);

                            if (data.readyRequired !== undefined) document.getElementById('readyToggle').checked = data.readyRequired;
                            if (data.tapoutEnabled !== undefined) document.getElementById('tapoutToggle').checked = data.tapoutEnabled;
                            if (data.remoteAudioEnabled !== undefined) document.getElementById('remoteAudioToggle').checked = data.remoteAudioEnabled;
                        }
                    })
                    .catch(e => console.error(e));
            }, 1000);

            function updateStatus(id, isPaired, viaLora, viaEspNow) {
                let el = document.getElementById(id);
                if (!isPaired) {
                    el.textContent = 'OPEN';
                    el.style.color = 'red';
                    return;
                }
                let via = (viaLora && viaEspNow) ? 'LoRa+ESP-NOW' : viaLora ? 'LoRa' : 'ESP-NOW';
                el.textContent = `PAIRED (${via})`;
                el.style.color = 'green';
            }
        </script>
    </body>
    </html>
)rawliteral";

#endif