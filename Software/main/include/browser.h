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

        <div id="pairingBanner">PAIRING MODE ACTIVE...</div>

        <div id="settingsGrid">
        <div id="timerControlsSection" class="status">
            <p id="countdown">02:00</p>

            <div id="timerControls">
                <div id="manualTimeSection" style="margin-bottom: 20px;">
                    <input type="number" id="manualMin" min="0" max="60"
                        oninput="this.value = !!this.value && Math.abs(this.value) >= 0 ? Math.min(Math.abs(this.value), 60) : null"
                        placeholder="MM"> :
                    <input type="number" id="manualSec" min="0" max="60"
                        oninput="this.value = !!this.value && Math.abs(this.value) >= 0 ? Math.min(Math.abs(this.value), 60) : null"
                        placeholder="SS">
                    <button id="setTimeBtn" class="small-btn" onclick="applyTime()" style="background:green; color:white;">Set Time</button>
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
            <strong>System Status:</strong><br>
            Red: <span id="redStat" style="color:red;">OPEN</span> | 
            Blue: <span id="blueStat" style="color:red;">OPEN</span> | 
            Judge: <span id="judgeStat" style="color:red;">OPEN</span>
            
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

            <div id="clockSection" style="margin-top: 10px;">
                <input type="checkbox" id="clockToggle" onchange="toggleClockMode()"> 
                <label for="clockToggle" style="display:inline;">Enable Clock Mode</label>
            </div>
        </div>

        <div id="displaySection" class="status">
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
            
            setInterval(() => {
                fetch('/status')
                    .then(r => r.json())
                    .then(data => {
                        lastKnownState = data.state;
                        if (data.state !== "RUNNING" && data.state !== "PRE_COUNTDOWN_LOOP" && data.currentTime) {
                            document.getElementById('countdown').textContent = data.currentTime;
                        }

                        document.getElementById('pairingBanner').style.display = data.pairing ? 'block' : 'none';
                        updateStatus('redStat', data.red);
                        updateStatus('blueStat', data.blue);
                        updateStatus('judgeStat', data.judge);

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
                        } else {
                            countdownEl.style.color = 'white';
                        }

                        // 1. TIMER BUTTON LOCKOUT
                        const lockGroup = ['startBtn', 'resetBtn', 'switchBtn', 'setTimeBtn', 'pauseBtn'];
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

                        // 2. SETTINGS PANEL LOCKOUT
                        const shouldLockSettings = isRunning || isPaused || isTapout || isClockMode;
                        const sections = ['displaySection', 'wifiSection', 'manualTimeSection', 'audioSection', 'syncSection'];
                        sections.forEach(id => {
                            const el = document.getElementById(id);
                            if (el) {
                                if (shouldLockSettings) el.classList.add('blocked-feature');
                                else el.classList.remove('blocked-feature');
                            }
                        });

                        const statusSection = document.getElementById('systemStatusSection');
                        if (statusSection) {
                            if (isRunning || isPaused || isTapout) {
                                statusSection.classList.add('blocked-feature');
                            } else {
                                statusSection.classList.remove('blocked-feature');
                            }
                        }

                        // 3. INDIVIDUAL INPUT COMPONENT DISABLING
                        const inputs = ['pairBtn', 'wipeBtn', 'wifiSSID', 'wifiPass', 'wifiBtn', 'wifiWipeBtn', 'updateBtn', 'factoryResetBtn', 'clockToggle', 'readyToggle', 'tapoutToggle', 'colorPicker', 'brightSlider', 'flipToggle', 'audioToggle', 'remoteAudioToggle', 'syncIpInput', 'syncSaveBtn', 'syncClearBtn'];
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

            function updateStatus(id, isPaired) {
                let el = document.getElementById(id);
                el.textContent = isPaired ? 'PAIRED' : 'OPEN';
                el.style.color = isPaired ? 'green' : 'red';
            }
        </script>
    </body>
    </html>
)rawliteral";

#endif