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
            .blocked-feature { opacity: 0.4; pointer-events: none; cursor: not-allowed; }
            
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
                <button id="resetBtn" onclick="controlTimer('reset')" style="background:gray; color:white;">Reset</button>
                <button id="switchBtn" onclick="toggleTime()" style="background:blue; color:white;">Switch 2/3m</button>
            </div>
        </div>

        <div id="pairingBanner">PAIRING MODE ACTIVE...</div>
        
        <div class="status">
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
                <input type="range" id="brightSlider" min="10" max="230" onchange="applyBrightness()" value="230"></label>
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

            function applyColor() {
                const hex = document.getElementById('colorPicker').value.replace('#', '');
                fetch(`/setcolor?hex=${hex}`);
            }

            function applyBrightness() {
                const val = document.getElementById('brightSlider').value;
                fetch(`/setbrightness?val=${val}`);
            }

            function toggleClockMode() {
                const clockToggle = document.getElementById('clockToggle');
                const timerActive = (lastKnownState === "RUNNING" || lastKnownState === "PAUSED" || lastKnownState.includes("PRE_COUNTDOWN"));
                
                if (clockToggle.checked && timerActive) {
                    clockToggle.checked = false; 
                    return; 
                }

                isLockingUI = true; 
                if (clockToggle.checked) {
                    const now = new Date();
                    fetch(`/synctime?h=${now.getHours()}&m=${now.getMinutes()}&s=${now.getSeconds()}`);
                    updateControls(true);
                } else {
                    controlTimer('reset');
                    updateControls(false);
                }
                
                setTimeout(() => { isLockingUI = false; }, 1500);
            }

            function updateControls(isClock) {
                const timerControls = document.getElementById('timerControls');
                if (isClock) {
                    timerControls.classList.add('disabled-ui');
                } else {
                    timerControls.classList.remove('disabled-ui');
                }
            }

            function controlTimer(action) { fetch(`/control?cmd=${action}`); }
            function toggleTime() { fetch('/control?cmd=switch'); }
            function toggleReady() { 
                fetch(`/control?cmd=readytoggle&state=${document.getElementById('readyToggle').checked ? "on" : "off"}`); 
            }
            function startPairing() { fetch('/pair'); }
            function wipeRemotes() { 
                if(confirm("Wipe all remotes?")) fetch('/clear_remotes'); 
            }
            function applyTime() { 
                fetch(`/settime?m=${document.getElementById('manualMin').value || 0}&s=${document.getElementById('manualSec').value || 0}`); 
            }
            
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
                        document.getElementById('readyToggle').checked = data.readyRequired;
                        
                        const isRunning = (data.state === "RUNNING" || data.state.includes("PRE_COUNTDOWN"));
                        const isPaused = (data.state === "PAUSED");
                        const isIdle = (data.state === "IDLE" || data.state === "FINISHED" || data.state === "CLOCK_MODE");

                        // 1. TIMER BUTTON VISUALS
                        const startBtn = document.getElementById('startBtn');
                        const pauseBtn = document.getElementById('pauseBtn');
                        const resetBtn = document.getElementById('resetBtn');

                        if (isRunning) { resetBtn.classList.add('blocked-feature'); resetBtn.disabled = true; }
                        else { resetBtn.classList.remove('blocked-feature'); resetBtn.disabled = false; }

                        if (!isRunning) { pauseBtn.classList.add('blocked-feature'); pauseBtn.disabled = true; }
                        else { pauseBtn.classList.remove('blocked-feature'); pauseBtn.disabled = false; }

                        if (isRunning) { startBtn.classList.add('blocked-feature'); startBtn.disabled = true; }
                        else { startBtn.classList.remove('blocked-feature'); startBtn.disabled = false; }

                        // 2. TIME SETTINGS (Editable during PAUSED)
                        const timeSection = document.getElementById('manualTimeSection');
                        const switchBtn = document.getElementById('switchBtn');
                        const timeInputs = ['manualMin', 'manualSec', 'setTimeBtn', 'switchBtn'];

                        if (isRunning) {
                            timeSection.classList.add('blocked-feature');
                            switchBtn.classList.add('blocked-feature');
                            timeInputs.forEach(id => document.getElementById(id).disabled = true);
                        } else {
                            timeSection.classList.remove('blocked-feature');
                            switchBtn.classList.remove('blocked-feature');
                            timeInputs.forEach(id => document.getElementById(id).disabled = false);
                        }

                        // 3. SYSTEM SETTINGS (Locked for RUNNING + PAUSED)
                        // Includes Display Section
                        const systemGroups = ['pairingControls', 'wifiSection', 'clockSection', 'readySection', 'displaySection'];
                        const systemInputs = ['pairBtn', 'wipeBtn', 'wifiSSID', 'wifiPass', 'wifiBtn', 'clockToggle', 'readyToggle', 'colorPicker', 'brightSlider'];
                        
                        const shouldLockSystem = !isIdle;

                        systemGroups.forEach(id => {
                            const el = document.getElementById(id);
                            if (shouldLockSystem) el.classList.add('blocked-feature');
                            else el.classList.remove('blocked-feature');
                        });

                        systemInputs.forEach(id => {
                            const el = document.getElementById(id);
                            if (el) el.disabled = shouldLockSystem;
                        });

                        if (!isLockingUI) {
                            const isClockMode = (data.state === "CLOCK_MODE");
                            document.getElementById('clockToggle').checked = isClockMode;
                            updateControls(isClockMode);
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