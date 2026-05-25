#ifndef BROWSER_H
#define BROWSER_H

const char* html = R"rawliteral(
    <!DOCTYPE html>
    <html lang="en">
    <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>ESP32 Countdown Timer</title>
        <style>
            body { font-family: Arial, sans-serif; text-align: center; background-color: black; color: white; }
            #countdown { font-size: 48px; color: white; }
            button { font-size: 24px; margin: 10px; }
        </style>
    </head>
    <body>
        <h1>ESP32 Countdown Timer</h1>
        <p id="countdown">02:00</p>
        <button onclick="controlTimer('start')">Start</button>
        <button onclick="controlTimer('pause')">Pause</button>
        <button onclick="controlTimer('reset')">Reset</button>
        <button onclick="toggleTime()">Switch 2min/3min</button>
        <script>
            let webSocket = new WebSocket(`ws://${window.location.hostname}:81/`);
            webSocket.onmessage = function(event) {
                document.getElementById('countdown').textContent = event.data;
            };
    
            function controlTimer(action) {
                fetch(`/control?cmd=${action}`);
            }
    
            function toggleTime() {
                fetch('/control?cmd=switch');
            }
        </script>
    </body>
    </html>
    )rawliteral";

#endif