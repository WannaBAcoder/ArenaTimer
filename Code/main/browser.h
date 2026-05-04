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
            button { font-size: 18px; margin: 5px; padding: 12px 20px; cursor: pointer; border-radius: 5px; border: none; }
            .small-btn { font-size: 14px; padding: 8px 15px; }
            .flex-row { display: flex; justify-content: center; gap: 10px; margin: 10px 0; }
            .status { margin: 15px auto; font-size: 16px; border: 1px solid #444; padding: 10px; border-radius: 8px; max-width: 400px; }
            #pairingBanner { display: none; background: #0000ff; padding: 15px; margin: 10px; font-weight: bold; border-radius: 5px; }
            input[type="number"] { padding: 8px; width: 50px; text-align: center; }
            label { font-size: 16px; margin: 10px; display: block; }
        </style>
    </head>
    <body>
        <h1>Battle Timer</h1>
        
        <p id="countdown">02:00</p>
        <div style="margin-bottom: 20px;">
            <input type="number" id="manualMin" placeholder="MM"> :
            <input type="number" id="manualSec" placeholder="SS">
            <button class="small-btn" onclick="applyTime()" style="background:green; color:white;">Set Time</button>
        </div>
        
        <div>
            <button onclick="controlTimer('start')" style="background:green; color:white;">Start</button>
            <button onclick="controlTimer('pause')" style="background:orange;">Pause</button>
            <button onclick="controlTimer('reset')" style="background:gray; color:white;">Reset</button>
            <button onclick="toggleTime()" style="background:blue; color:white;">Switch 2/3m</button>
        </div>

        <div id="pairingBanner">PAIRING MODE ACTIVE... Press the remote button now.</div>
        
        <div class="status">
            <strong>System Status:</strong><br>
            Red: <span id="redStat" style="color:red;">OPEN</span> | 
            Blue: <span id="blueStat" style="color:red;">OPEN</span> | 
            Judge: <span id="judgeStat" style="color:red;">OPEN</span>
            
            <div class="flex-row" style="margin-top:10px;">
                <button class="small-btn" onclick="startPairing()" style="background:orange;">Pair Remotes</button>
                <button class="small-btn" onclick="wipeRemotes()" style="background:red; color:white;">Wipe All</button>
            </div>
            
            <div>
                <input type="checkbox" id="readyToggle" onchange="toggleReady()"> 
                <label for="readyToggle" style="display:inline;">Require Driver Ready</label>
            </div>
        </div>

        <div class="status">
            <h3>WiFi Settings</h3>


            <form action="/setwifi" method="POST">
                <input name="ssid" placeholder="SSID" required style="padding:5px; margin:5px;"><br>
                <input name="pass" type="password" placeholder="Password" required style="padding:5px; margin:5px;"><br>
                <button type="submit" class="small-btn" style="background:gray; color:white;">Save & Reboot</button>
            </form>
        </div>

        <script>
            // WebSocket for Timer Updates
            let webSocket = new WebSocket(`ws://${window.location.hostname}:81/`);
            webSocket.onmessage = function(event) {
                document.getElementById('countdown').textContent = event.data;
            };

            // Control Functions
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
            
            // Polling
            setInterval(() => {
                fetch('/status')
                    .then(r => r.json())
                    .then(data => {
                        document.getElementById('pairingBanner').style.display = data.pairing ? 'block' : 'none';
                        updateStatus('redStat', data.red); 
                        updateStatus('blueStat', data.blue); 
                        updateStatus('judgeStat', data.judge);
                        document.getElementById('readyToggle').checked = data.readyRequired;
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