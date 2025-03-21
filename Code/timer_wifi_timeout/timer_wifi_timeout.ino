#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <FastLED.h> // Include FastLED library

const char* ssid = "SEMO Combat Robotics";
const char* password = "Mulehair";

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// Pin assignments
#define RESET_BTN 27
#define PAUSE_BTN 14
#define START_BTN 12 // OK, boot fails if pulled high, strapping pin
#define TIME_SEL_SW 13

// LED pin assignments and setup
#define DIGIT_PIN   5
#define BORDER_PIN  17
#define DIGIT_LED_COUNT 202
#define BORDER_LED_COUNT 140
CRGB digit_leds[DIGIT_LED_COUNT];
CRGB border_leds[BORDER_LED_COUNT];

int countdown_time = 120; // Default to 2 minutes (120 seconds)
int current_time = countdown_time;
bool is_running = false;
bool lastTimeSelState = false;

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

void handleRoot() {
  server.send(200, "text/html", html);
}

void handleControl() {
  String cmd = server.arg("cmd");
  if (cmd == "start") {
    is_running = true;
  } else if (cmd == "pause") {
    is_running = false;
  } else if (cmd == "reset") {
    is_running = false;
    current_time = countdown_time;
    updateClient(); // Update the client immediately
    updateLEDs();
  } else if (cmd == "switch") {
    countdown_time = (countdown_time == 120) ? 180 : 120;
    current_time = countdown_time;
    updateClient(); // Update the client immediately
    updateLEDs();
  }
  server.send(200, "text/plain", "OK");
}

void updateClient() {
  int minutes = current_time / 60;
  int seconds = current_time % 60;
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, seconds);
  webSocket.broadcastTXT(buffer);
}

void updateTimer() {
  if (is_running && current_time > 0) {
    current_time--;
  }
  updateClient();
  updateLEDs();
}

void checkButtons() {
  if (digitalRead(START_BTN) == LOW) {
    is_running = true;
  }
  if (digitalRead(PAUSE_BTN) == LOW) {
    is_running = false;
  }
  if (digitalRead(RESET_BTN) == LOW) {
    is_running = false;
    current_time = countdown_time;
    updateClient(); // Update the client immediately
    updateLEDs();
  }
  bool timeSelState = digitalRead(TIME_SEL_SW) == LOW;
  if (timeSelState != lastTimeSelState) {
    lastTimeSelState = timeSelState;
    countdown_time = timeSelState ? 180 : 120;
    current_time = countdown_time;
    updateClient(); // Update the client immediately
    updateLEDs();
  }
}

void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  // Nothing to do here for now
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  // Timeout mechanism for WiFi connection
  unsigned long wifiStartTime = millis();
  const unsigned long wifiTimeout = 60000; // 1 minute timeout

  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
    if (millis() - wifiStartTime > wifiTimeout) {
      Serial.println("WiFi connection timed out. Proceeding without WiFi...");
      break;
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    Serial.println(WiFi.localIP());

    server.on("/", handleRoot);
    server.on("/control", handleControl);
    server.begin();

    webSocket.begin();
    webSocket.onEvent(onWebSocketEvent);
  } else {
    Serial.println("WiFi not connected. Skipping server and WebSocket setup.");
  }

  // Set up the buttons and switch pins
  pinMode(RESET_BTN, INPUT_PULLUP);
  pinMode(PAUSE_BTN, INPUT_PULLUP);
  pinMode(START_BTN, INPUT_PULLUP);
  pinMode(TIME_SEL_SW, INPUT_PULLUP);
  lastTimeSelState = digitalRead(TIME_SEL_SW) == LOW;

  // Initialize LED strips
  FastLED.addLeds<NEOPIXEL, DIGIT_PIN>(digit_leds, DIGIT_LED_COUNT);
  FastLED.addLeds<NEOPIXEL, BORDER_PIN>(border_leds, BORDER_LED_COUNT);
  FastLED.setBrightness(255); // Set BRIGHTNESS to max

  setBorder();
  
  // Start the timer update task
  xTaskCreate(
    [](void*) {
      TickType_t lastWakeTime = xTaskGetTickCount();
      while (true) {
        if (is_running) {
          updateTimer();
        }
        vTaskDelayUntil(&lastWakeTime, pdMS_TO_TICKS(1000));
      }
    },
    "TimerTask",
    2048,
    nullptr,
    1,
    nullptr
  );

  // Start the button check task
  xTaskCreate(
    [](void*) {
      while (true) {
        checkButtons();
        vTaskDelay(pdMS_TO_TICKS(50)); // Check buttons every 50ms
      }
    },
    "ButtonCheckTask",
    2048,
    nullptr,
    1,
    nullptr
  );
}

void loop() {
  server.handleClient();
  webSocket.loop();
}

void setDigit(int digit, int offset, bool inverted) {
  static const uint8_t digitMap[10][7] = {
    {1, 1, 1, 1, 1, 1, 0},  // 0
    {0, 1, 1, 0, 0, 0, 0},  // 1
    {1, 1, 0, 1, 1, 0, 1},  // 2
    {1, 1, 1, 1, 0, 0, 1},  // 3
    {0, 1, 1, 0, 0, 1, 1},  // 4
    {1, 0, 1, 1, 0, 1, 1},  // 5
    {1, 0, 1, 1, 1, 1, 1},  // 6
    {1, 1, 1, 0, 0, 0, 0},  // 7
    {1, 1, 1, 1, 1, 1, 1},  // 8
    {1, 1, 1, 1, 0, 1, 1}   // 9
  };

  // Remap segments for inverted digits
  static const uint8_t invertedMap[7] = {3, 4, 5, 0, 1, 2, 6};

  for (int i = 0; i < 7; i++) {
    int segmentIndex = inverted ? invertedMap[i] : i;
    int ledIndex = offset + segmentIndex * 7;
    for (int j = 0; j < 7; j++) {
      if (digitMap[digit][i]) {
        digit_leds[ledIndex + j] = CRGB::Red;  // Red color
      } else {
        digit_leds[ledIndex + j] = CRGB::Black;  // Turn off
      }
    }
  }
}

void setColon() {
  for (int i = 0; i < 3; i++) {
    digit_leds[98 + i] = CRGB::Red;  // Colon dot
    digit_leds[199 + i] = CRGB::Red;  // Colon dot
  }
}

void updateLEDs() {
  int minutes = current_time / 60;
  int seconds = current_time % 60;

  // Convert minutes and seconds to individual digits
  int digit1 = minutes / 10;
  int digit2 = minutes % 10;
  int digit3 = seconds / 10;
  int digit4 = seconds % 10;

  // Update each 7-segment digit and colon dots
  setDigit(digit1, 0, false);     // First digit
  setDigit(digit2, 49, false);    // Second digit
  setColon();                     // Colon dots
  setDigit(digit4, 101, true);    // Third digit (upside down and backwards)
  setDigit(digit3, 150, true);    // Fourth digit (upside down and backwards)

  FastLED.show();  // Update the strip to show the changes
}

void setBorder()
{
   // Set border LEDs
  int halfBorderCount = BORDER_LED_COUNT / 2;
  for (int i = 0; i < halfBorderCount; i++) {
    border_leds[i] = CRGB::Blue; // First half blue
  }
  for (int i = halfBorderCount; i < BORDER_LED_COUNT; i++) {
    border_leds[i] = CRGB::Red;  // Second half red
  }

  FastLED.show(); // Show the initialized border LED colors
}
