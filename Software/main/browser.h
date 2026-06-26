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
        </style>
    </head>
    <body>
        <h1>Battle Timer</h1>
        
        <p id="countdown">02:00</p>
        
        <div id="timerControls">
            <div id="manualTimeSection" style="margin-bottom: 20px;">
                <input type="number" id="manualMin" placeholder="MM"> :
                <input type="number" id="manualSec" placeholder="SS">
                <button id="setTimeBtn" class="small-btn" onclick="applyTime()" style="background:green; color:white;">Set Time</button>
            </div>
            
            <div>
                <button id="startBtn" onclick="controlTimer('start')" style="background:green; color:white;">Start</button>
                <button id="pauseBtn" onclick="controlTimer('pause')" style="background:orange;">Pause</button>
                <button id="resetBtn" onclick="controlTimer('reset')" style="background:#8b0000; color:white;">Reset</button>
                <button id="switchBtn" onclick="toggleTime()" style="background:blue; color:white;">Switch 2/3m</button>
            </div>
        </div>

        <div id="pairingBanner">PAIRING MODE ACTIVE...</div>
        
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

            <div id="slaveSection" style="margin-top: 10px; border-top: 1px solid #444; padding-top: 10px;">
                <input type="checkbox" id="slaveToggle" onchange="toggleSlaveMode()"> 
                <label for="slaveToggle" style="display:inline; color: #ff3333; font-weight: bold;">Enable Passive Slave Mode</label>
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

        <div id="wifiSection" class="status">
            <h3>WiFi Settings</h3>
            <form action="/setwifi" method="POST">
                <input id="wifiSSID" name="ssid" placeholder="SSID" required autocapitalize="none" autocorrect="off" style="padding:5px; margin:5px;"><br>
                <input id="wifiPass" name="pass" type="text" placeholder="Password" required autocapitalize="none" autocorrect="off" style="padding:5px; margin:5px;"><br>
                <button id="wifiBtn" type="submit" class="small-btn" style="background:gray; color:white;">Save & Reboot</button>
            </form>
        </div>

        <script>
            let isLockingUI = false; 
            let lastKnownState = "IDLE";
            let webSocket = new WebSocket(`ws://${window.location.hostname}:81/`);

            webSocket.onmessage = function(event) {
                document.getElementById('countdown').textContent = event.data;
            };

            window.onload = function() {
                fetch('/status')
                .then(r => r.json())
                .then(data => {
                    if(data.brightness !== undefined) document.getElementById('brightSlider').value = data.brightness;
                    if(data.digitColor) document.getElementById('colorPicker').value = "#" + data.digitColor;
                    if(data.displayInverted !== undefined) document.getElementById('flipToggle').checked = data.displayInverted;
                    if(data.readyRequired !== undefined) document.getElementById('readyToggle').checked = data.readyRequired;
                    if(data.tapoutEnabled !== undefined) document.getElementById('tapoutToggle').checked = data.tapoutEnabled;
                    if(data.slaveModeEnabled !== undefined) document.getElementById('slaveToggle').checked = data.slaveModeEnabled;
                    
                    if(data.audioEnabled !== undefined) document.getElementById('audioToggle').checked = data.audioEnabled;
                    if(data.remoteAudioEnabled !== undefined) document.getElementById('remoteAudioToggle').checked = data.remoteAudioEnabled;
                    if(data.audioOutput !== undefined) {
                        let radioBtn = document.querySelector(`input[name="outputSelect"][value="${data.audioOutput}"]`);
                        if(radioBtn) radioBtn.checked = true;
                    }
                    
                    const isClockMode = (data.state === "CLOCK_MODE");
                    document.getElementById('clockToggle').checked = isClockMode;
                    updateControls(isClockMode || data.slaveModeEnabled);
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

            // Fixed trailing brackets from source code tracking
            function toggleFlip() { fetch('/flip'); }

            // Dynamic background handler mapping variables
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

            function toggleSlaveMode() {
                const state = document.getElementById('slaveToggle').checked ? "on" : "off";
                fetch(`/control?cmd=slavetoggle&state=${state}`)
                .then(() => location.reload()); 
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
            // Fixed unclosed wrapper block elements
            function startPairing() { fetch('/pair'); }
            function wipeRemotes() { if(confirm("Wipe all remotes?")) fetch('/clear_remotes'); }
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
                        
                        const isRunning = (data.state === "RUNNING" || data.state === "PRE_COUNTDOWN_LOOP");
                        const isPaused = (data.state === "PAUSED");
                        const isTapout = (data.state === "TAPOUT");
                        const isIdle = (data.state === "IDLE" || data.state === "FINISHED" || data.state === "CLOCK_MODE");

                        const timerControls = document.getElementById('timerControls');
                        if (isTapout || isPaused || isIdle) {
                            if (!data.slaveModeEnabled) timerControls.classList.remove('disabled-ui');
                        }

                        const countdownEl = document.getElementById('countdown');
                        if (isTapout) {
                            countdownEl.style.color = data.tapoutBlue ? '#0011ff' : '#ff3333';
                        } else {
                            countdownEl.style.color = 'white';
                        }

                        // 1. TIMER BUTTON LOCKOUT
                        const lockGroup = ['startBtn', 'resetBtn', 'switchBtn', 'setTimeBtn'];
                        lockGroup.forEach(id => {
                            const btn = document.getElementById(id);
                            if (data.slaveModeEnabled || isRunning) {
                                btn.classList.add('blocked-feature');
                                btn.disabled = true;
                            } else if ((isPaused || isTapout) && id === 'resetBtn') {
                                btn.classList.remove('blocked-feature');
                                btn.disabled = false;
                            } else if (isPaused && id === 'startBtn') {
                                btn.classList.remove('blocked-feature');
                                btn.disabled = false;
                            } else if (!isIdle && !isTapout) {
                                btn.classList.add('blocked-feature');
                                btn.disabled = true;
                            } else {
                                btn.classList.remove('blocked-feature');
                                btn.disabled = false;
                            }
                        });

                        document.getElementById('pauseBtn').disabled = !isRunning || data.slaveModeEnabled;
                        if (!isRunning || data.slaveModeEnabled) document.getElementById('pauseBtn').classList.add('blocked-feature');
                        else document.getElementById('pauseBtn').classList.remove('blocked-feature');

                        // 2. SETTINGS PANEL LOCKOUT
                        // FIX: When a match is running, lock down the entire systemStatusSection panel completely
                        const shouldLockSettings = (!isIdle && !isTapout) || data.slaveModeEnabled;
                        const sections = ['systemStatusSection', 'displaySection', 'wifiSection', 'manualTimeSection', 'audioSection'];
                        sections.forEach(id => {
                            const el = document.getElementById(id);
                            if (el) {
                                if (shouldLockSettings) {
                                    el.classList.add('blocked-feature');
                                } else {
                                    el.classList.remove('blocked-feature');
                                }
                            }
                        });

                        // 3. INDIVIDUAL INPUT COMPONENT DISABLING
                        // FIX: Added slaveToggle to the input list so it gets disabled programmatically when a match runs
                        const inputs = ['pairBtn', 'wipeBtn', 'wifiSSID', 'wifiPass', 'wifiBtn', 'clockToggle', 'readyToggle', 'tapoutToggle', 'colorPicker', 'brightSlider', 'flipToggle', 'audioToggle', 'remoteAudioToggle', 'slaveToggle'];
                        inputs.forEach(id => {
                            const el = document.getElementById(id);
                            if (el) {
                                if (id === 'tapoutToggle' && !data.slaveModeEnabled) {
                                    el.disabled = false; 
                                } else {
                                    el.disabled = shouldLockSettings;
                                }
                            }
                        });

                        const radioGroup = document.querySelectorAll('input[name="outputSelect"]');
                        radioGroup.forEach(radio => { radio.disabled = shouldLockSettings; });

                        if (!isLockingUI) {
                            const isClockMode = (data.state === "CLOCK_MODE");
                            document.getElementById('clockToggle').checked = isClockMode;
                            if(!data.slaveModeEnabled) updateControls(isClockMode);

                            if (data.readyRequired !== undefined) document.getElementById('readyToggle').checked = data.readyRequired;
                            if (data.tapoutEnabled !== undefined) document.getElementById('tapoutToggle').checked = data.tapoutEnabled;
                            if (data.remoteAudioEnabled !== undefined) document.getElementById('remoteAudioToggle').checked = data.remoteAudioEnabled;
                            if (data.slaveModeEnabled !== undefined) document.getElementById('slaveToggle').checked = data.slaveModeEnabled;
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