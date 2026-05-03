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
            body { font-family: Arial, sans-serif; text-align: center; background-color: black; color: white; }
            #countdown { font-size: 48px; color: white; }
            button { font-size: 20px; margin: 10px; padding: 10px; cursor: pointer; }
            .status { margin: 20px; font-size: 18px; font-weight: bold; border: 1px solid #444; padding: 10px; border-radius: 8px; }
            #pairingBanner { display: none; background: blue; padding: 15px; margin: 10px; font-weight: bold; border-radius: 5px; }
            label { font-size: 18px; margin-top: 20px; display: block; }
        </style>
    </head>
    <body>
        <h1>ESP32 Battle Timer</h1>
        <p id="countdown">02:00</p>
        
        <button onclick="controlTimer('start')">Start</button>
        <button onclick="controlTimer('pause')">Pause</button>
        <button onclick="controlTimer('reset')">Reset</button>
        <button onclick="toggleTime()">Switch 2min/3min</button>
        
        <label>
            <input type="checkbox" id="readyToggle" onchange="toggleReady()"> Require Driver Ready
        </label>

        <div id="pairingBanner">PAIRING MODE ACTIVE... Press the remote button now.</div>
        
        <div class="status">
            Red: <span id="redStat" style="color:red;">OPEN</span> | 
            Blue: <span id="blueStat" style="color:red;">OPEN</span> | 
            Judge: <span id="judgeStat" style="color:red;">OPEN</span>
        </div>
        
        <button onclick="startPairing()" style="background:orange;">Pair Remotes</button>
        <button onclick="wipeRemotes()" style="background:red; color:white;">WIPE ALL REMOTES</button>

        <script>
            // WebSocket for Timer Updates
            let webSocket = new WebSocket(`ws://${window.location.hostname}:81/`);
            webSocket.onmessage = function(event) {
                document.getElementById('countdown').textContent = event.data;
            };

            // Existing Control Functions
            function controlTimer(action) { fetch(`/control?cmd=${action}`); }
            function toggleTime() { fetch('/control?cmd=switch'); }
            
            function toggleReady() {
                let checkbox = document.getElementById('readyToggle');
                let state = checkbox.checked ? "on" : "off";
                fetch(`/control?cmd=readytoggle&state=${state}`);
            }

            // Pairing & System Functions
            function startPairing() { fetch('/pair'); }
            
            function wipeRemotes() {
                if(confirm("Are you sure you want to wipe all remote bindings?")) {
                    fetch('/clear_remotes').then(() => {
                        alert("Remotes wiped. You can now re-pair.");
                    });
                }
            }

            // Polling for Status Updates (1Hz)
            setInterval(() => {
                fetch('/status')
                    .then(response => response.json())
                    .then(data => {
                        // Toggle Pairing Banner
                        document.getElementById('pairingBanner').style.display = data.pairing ? 'block' : 'none';
                        
                        // Update Status Labels
                        updateStatus('redStat', data.red);
                        updateStatus('blueStat', data.blue);
                        updateStatus('judgeStat', data.judge);
                        
                        // Sync Checkbox State
                        document.getElementById('readyToggle').checked = data.readyRequired;
                    })
                    .catch(error => console.error('Status fetch failed:', error));
            }, 1000);

            function updateStatus(id, isPaired) {
                let el = document.getElementById(id);
                el.textContent = isPaired ? 'PAIRED' : 'OPEN';
                el.style.color = isPaired ? 'green' : 'red';
            }
        </script>

        <div class="status" style="margin-top: 30px;">
            <h3>WiFi Settings</h3>
            <form action="/setwifi" method="POST">
                <input name="ssid" placeholder="SSID" required style="padding:5px; margin:5px;"><br>
                <input name="pass" type="password" placeholder="Password" required style="padding:5px; margin:5px;"><br>
                <button type="submit" style="background:gray; color:white;">Save & Reboot</button>
            </form>
        </div>
    </body>
    </html>
)rawliteral";

#endif