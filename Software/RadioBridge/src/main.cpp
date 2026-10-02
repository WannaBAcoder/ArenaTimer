#include <Arduino.h>

// Raw AT-command passthrough to the RA-08H, for interactively probing what
// its stock firmware actually supports (LoRaWAN-only AT set for sure;
// AT+TEST=... P2P mode and AT+CTX=... transparent-transmission mode are
// both unconfirmed guesses - see project memory: RA-08H firmware
// uncertainty). Type commands in the serial monitor, see raw responses.
//
// Pins match Software/main/include/config.h as of commit ee06693.
#define RADIO_RST_PIN 25
#define RADIO_UART_TX 23 // ESP32 TX -> RA-08H LPRXD
#define RADIO_UART_RX 22 // ESP32 RX <- RA-08H TXD

// Boot log came through clean at 115200 but zero response to AT commands -
// trying 9600 next since Ai-Thinker's manual notes the LPUART-based AT
// interface caps out there (AT+CGBR note: "because LPUART is used, baud
// rate cannot exceed 9600").
#define RADIO_BAUD 9600

HardwareSerial radioSerial(2); // UART2

void setup() {
    Serial.begin(115200);
    delay(500);

    radioSerial.begin(RADIO_BAUD, SERIAL_8N1, RADIO_UART_RX, RADIO_UART_TX);

    pinMode(RADIO_RST_PIN, OUTPUT);
    digitalWrite(RADIO_RST_PIN, LOW);
    delay(10);
    digitalWrite(RADIO_RST_PIN, HIGH);
    delay(500); // let the module boot before talking to it

    Serial.println();
    Serial.println("=== RA-08H AT Bridge ===");
    Serial.printf("UART2 @ %d baud, RX=GPIO%d, TX=GPIO%d, RST=GPIO%d\n",
                   RADIO_BAUD, RADIO_UART_RX, RADIO_UART_TX, RADIO_RST_PIN);
    Serial.println("Type AT commands and press Enter. Raw responses print below.");
    Serial.println("If you see only garbage, retry with RADIO_BAUD set to 9600.");
    Serial.println();
    Serial.println("Things worth trying first:");
    Serial.println("  AT              - should just answer OK if alive at all");
    Serial.println("  AT+CGMI?        - manufacturer id (confirmed to exist in the stock LoRaWAN AT set)");
    Serial.println("  AT+CGMR?        - firmware revision string");
    Serial.println("  AT+TEST=?       - UNCONFIRMED: raw P2P test-mode command, may not exist on stock firmware");
    Serial.println("  AT+CTX=?        - UNCONFIRMED: transparent-transmission firmware command, only if reflashed");
    Serial.println("========================");
    Serial.println();
}

void loop() {
    // USB -> radio: normalize a bare '\n' to "\r\n" since AT firmware
    // expects CRLF-terminated lines and most terminals only send '\n'.
    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n') {
            radioSerial.write('\r');
            radioSerial.write('\n');
        } else if (c != '\r') {
            radioSerial.write(c);
        }
    }

    // radio -> USB: passed through completely raw, no interpretation.
    while (radioSerial.available()) {
        Serial.write(radioSerial.read());
    }
}
