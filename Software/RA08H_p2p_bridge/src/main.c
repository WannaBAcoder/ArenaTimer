// Bare-bones LoRa <-> host UART bridge for ArenaTimer.
//
// Speaks the same text framing the host code (TimerRemote/src/radio.cpp,
// Software/main/src/loraRemotes.cpp) already implements, so neither host
// file needs to change:
//   host -> radio:  AT+TEST=TXLRPKT,"<HEX>"\n   -> transmits <HEX> over LoRa
//   radio -> host:  +TEST: RXLRPKT,"<HEX>",<rssi>,<snr>\r\n  on packet RX
//
// Runtime-adjustable RF settings, so range/power tuning doesn't need a
// radio reflash - only the (much cheaper to reflash) host MCU needs to send
// its desired config after every boot:
//   host -> radio:  AT+RFCFG=<power>,<sf>,<bw>,<cr>\n
//                     power: -3..22 (dBm, SX1262 max)
//                     sf:    5..12  (spreading factor)
//                     bw:    0..2   (0=125kHz, 1=250kHz, 2=500kHz)
//                     cr:    1..4   (coding rate, 1=4/5 .. 4=4/8)
//   radio -> host:  OK\r\n   or ERROR\r\n if out of range / malformed
//   host -> radio:  AT+RFCFG?\n           -> query current values
//   radio -> host:  +RFCFG: <power>,<sf>,<bw>,<cr>\r\nOK\r\n
//
// RFCFG changes are RAM-only, not persisted in the radio's own flash - they
// reset to the DEFAULT_* values below on every power-up/reset. SF and BW
// must match on both ends of a link or the two radios can't demodulate each
// other's packets; TX power is one-sided and only needs to change on the
// transmitting radio.
//
// Also runtime-adjustable: whether the radio sits in continuous receive or
// sleeps between transmits. A LoRa remote in this system only ever sends
// (button presses) and never needs to receive, so leaving it in continuous
// RX - the SX1262's most power-hungry state - burns battery for no reason.
// The timer, which must always be listening, needs the opposite:
//   host -> radio:  AT+RXMODE=<mode>\n
//                     mode: 0 = TX-only (sleep between sends - remotes)
//                           1 = continuous RX (default - the timer)
//   radio -> host:  OK\r\n   or ERROR\r\n if out of range
//   host -> radio:  AT+RXMODE?\n
//   radio -> host:  +RXMODE: <mode>\r\nOK\r\n
// Waking from Sleep() to transmit needs no special handling: any SPI
// transaction wakes the SX1262 automatically (a documented hardware
// feature), and this firmware already relies on that exact Sleep()->next
// action transition after every completed TX (see OnTxDone below).
//
// Runs identically on both the remote and timer boards - which side is
// "remote" vs "timer" is entirely a host-MCU concern, not a radio concern.
//
// UART pins match the board's actual wiring (see PCB Files netlists):
//   LPUART RX  = GPIOD pin 12 (GP60 / LPRXD/IO60) - host's TX arrives here
//   UART0  TX  = GPIOB pin 1  (GP17 / TXD/IO17)    - shared with the debug/
//                                                     flash port, but only
//                                                     TX is used here

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "delay.h"
#include "timer.h"
#include "radio.h"
#include "tremo_system.h"
#include "tremo_uart.h"
#include "tremo_lpuart.h"
#include "tremo_gpio.h"
#include "tremo_rcc.h"
#include "tremo_pwr.h"
#include "tremo_delay.h"
#include "rtc-board.h"

#if defined( REGION_US915 )
#define RF_FREQUENCY 915000000
#else
#error "Build with -DREGION_US915"
#endif

// Power-on defaults - overridden at runtime by AT+RFCFG (see header comment).
#define DEFAULT_TX_OUTPUT_POWER       14
#define DEFAULT_LORA_BANDWIDTH        0 // 125 kHz
#define DEFAULT_LORA_SPREADING_FACTOR 7
#define DEFAULT_LORA_CODINGRATE       1 // 4/5

#define LORA_PREAMBLE_LENGTH         8
#define LORA_SYMBOL_TIMEOUT          0
#define LORA_FIX_LENGTH_PAYLOAD_ON   false
#define LORA_IQ_INVERSION_ON         false
#define RX_TIMEOUT_VALUE             5000 // ms, re-armed on every timeout

#define RFCFG_POWER_MIN  (-3)
#define RFCFG_POWER_MAX  22
#define RFCFG_SF_MIN     5
#define RFCFG_SF_MAX     12
#define RFCFG_BW_MIN     0
#define RFCFG_BW_MAX     2
#define RFCFG_CR_MIN     1
#define RFCFG_CR_MAX     4

// AT+RXMODE values - see header comment.
#define RXMODE_TX_ONLY      0
#define RXMODE_CONTINUOUS   1
#define DEFAULT_RX_MODE     RXMODE_CONTINUOUS

// Bridge UART baud - proven reliable on this LPUART peripheral empirically
// (the stock AT firmware also caps out around here; 115200 is not safe on
// this peripheral, see project memory: RA-08H firmware uncertainty).
#define BRIDGE_BAUD 9600

#define MAX_PAYLOAD 32
#define CMD_BUF_LEN 96

static RadioEvents_t RadioEvents;

// Live RF config, RAM-only (see AT+RFCFG in the header comment).
static int8_t  currentPower = DEFAULT_TX_OUTPUT_POWER;
static uint8_t currentSF    = DEFAULT_LORA_SPREADING_FACTOR;
static uint8_t currentBW    = DEFAULT_LORA_BANDWIDTH;
static uint8_t currentCR    = DEFAULT_LORA_CODINGRATE;
static uint8_t currentRxMode = DEFAULT_RX_MODE;

static uint8_t RxPayload[MAX_PAYLOAD];
static uint16_t RxPayloadSize = 0;
static volatile bool RxPending = false;
static int16_t LastRssi = 0;
static int8_t LastSnr = 0;

static const char HEX_DIGITS[] = "0123456789ABCDEF";

static void hexEncode(const uint8_t* data, uint16_t len, char* out)
{
    for (uint16_t i = 0; i < len; i++) {
        out[i * 2]     = HEX_DIGITS[(data[i] >> 4) & 0xF];
        out[i * 2 + 1] = HEX_DIGITS[data[i] & 0xF];
    }
    out[len * 2] = '\0';
}

static int hexNibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static bool hexDecode(const char* hex, uint16_t hexLen, uint8_t* out, uint16_t* outLen)
{
    if (hexLen == 0 || (hexLen % 2) != 0) return false;
    uint16_t n = hexLen / 2;
    if (n > MAX_PAYLOAD) return false;
    for (uint16_t i = 0; i < n; i++) {
        int hi = hexNibble(hex[i * 2]);
        int lo = hexNibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    *outLen = n;
    return true;
}

// Re-arms continuous receive if that's the configured mode, otherwise sleeps
// instead - see AT+RXMODE in the header comment. Called after every TX/RX
// completion or timeout in place of an unconditional Radio.Rx().
static void rearmOrSleep(void)
{
    if (currentRxMode == RXMODE_CONTINUOUS) {
        Radio.Rx(RX_TIMEOUT_VALUE);
    } else {
        Radio.Sleep();
    }
}

void OnTxDone(void)
{
    Radio.Sleep();
    rearmOrSleep();
}

void OnRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr)
{
    Radio.Sleep();
    if (size <= MAX_PAYLOAD) {
        memcpy(RxPayload, payload, size);
        RxPayloadSize = size;
        LastRssi = rssi;
        LastSnr = snr;
        RxPending = true;
    }
    rearmOrSleep();
}

void OnTxTimeout(void)
{
    Radio.Sleep();
    rearmOrSleep();
}

void OnRxTimeout(void)
{
    rearmOrSleep();
}

void OnRxError(void)
{
    rearmOrSleep();
}

// Applies currentPower/currentSF/currentBW/currentCR to the radio, then
// re-arms receive or sleeps per currentRxMode. Called once at boot with the
// DEFAULT_* values, and again whenever AT+RFCFG changes them.
static void applyRadioConfig(void)
{
    Radio.Sleep();

    Radio.SetTxConfig(MODEM_LORA, currentPower, 0, currentBW,
                       currentSF, currentCR,
                       LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
                       true, 0, 0, LORA_IQ_INVERSION_ON, 3000);

    Radio.SetRxConfig(MODEM_LORA, currentBW, currentSF,
                       currentCR, 0, LORA_PREAMBLE_LENGTH,
                       LORA_SYMBOL_TIMEOUT, LORA_FIX_LENGTH_PAYLOAD_ON,
                       0, true, 0, 0, LORA_IQ_INVERSION_ON, true);

    rearmOrSleep();
}

static void lpuart_init_step(void)
{
    gpio_set_iomux(GPIOD, GPIO_PIN_12, 2); // LPUART_RX: GP60
    printf("CKPT: lpuart iomux done\r\n");

    lpuart_deinit(LPUART);
    printf("CKPT: lpuart deinit done\r\n");
    rcc_enable_peripheral_clk(RCC_PERIPHERAL_LPUART, true);
    printf("CKPT: lpuart clk enabled\r\n");

    lpuart_init_t lpuart_cfg;
    lpuart_cfg.baudrate         = BRIDGE_BAUD;
    lpuart_cfg.data_width       = LPUART_DATA_8BIT;
    lpuart_cfg.parity           = LPUART_PARITY_NONE;
    lpuart_cfg.stop_bits        = LPUART_STOP_1BIT;
    lpuart_cfg.low_level_wakeup = false;
    lpuart_cfg.start_wakeup     = false;
    lpuart_cfg.rx_done_wakeup   = false;
    lpuart_init(LPUART, &lpuart_cfg);
    printf("CKPT: lpuart_init done\r\n");
    lpuart_config_tx(LPUART, false);
    lpuart_config_rx(LPUART, true);
    printf("CKPT: lpuart config done\r\n");
}

static void board_init(void)
{
    rcc_enable_oscillator(RCC_OSC_XO32K, true);
    printf("CKPT: xo32k enabled\r\n");

    rcc_enable_peripheral_clk(RCC_PERIPHERAL_GPIOA, true);
    rcc_enable_peripheral_clk(RCC_PERIPHERAL_GPIOC, true);
    rcc_enable_peripheral_clk(RCC_PERIPHERAL_GPIOD, true);
    printf("CKPT: gpio clocks done\r\n");

    rcc_enable_peripheral_clk(RCC_PERIPHERAL_PWR, true);
    printf("CKPT: pwr clock done\r\n");

    rcc_enable_peripheral_clk(RCC_PERIPHERAL_RTC, true);
    printf("CKPT: rtc clock done\r\n");

    rcc_enable_peripheral_clk(RCC_PERIPHERAL_SAC, true);
    printf("CKPT: sac clock done\r\n");

    rcc_enable_peripheral_clk(RCC_PERIPHERAL_LORA, true);
    printf("CKPT: lora clock done\r\n");

    delay_ms(100);
    printf("CKPT: delay done\r\n");

    pwr_xo32k_lpm_cmd(true);
    printf("CKPT: xo32k lpm done\r\n");

    lpuart_init_step();

    RtcInit();
    printf("CKPT: rtc init done\r\n");
}

static char cmdBuf[CMD_BUF_LEN];
static uint8_t cmdLen = 0;

static void handleTxCommand(const char* line, const char* prefix, size_t prefixLen)
{
    const char* hexStart = line + prefixLen;
    const char* hexEnd = strchr(hexStart, '"');
    if (!hexEnd) {
        printf("ERROR\r\n");
        return;
    }

    uint8_t payload[MAX_PAYLOAD];
    uint16_t payloadLen = 0;
    if (!hexDecode(hexStart, (uint16_t)(hexEnd - hexStart), payload, &payloadLen)) {
        printf("ERROR\r\n");
        return;
    }

    printf("OK\r\n");
    Radio.Send(payload, (uint8_t)payloadLen);
    (void)prefix;
}

// Parses one signed decimal integer at *cursor, advancing past it and past
// one following comma if present. Deliberately not sscanf(): that pulls in
// newlib's real scanf machinery, which needs syscall stubs (_sbrk, _exit,
// etc.) this bare-metal build doesn't provide - the same reason printf is
// wrapped to a minimal implementation instead of using newlib's directly.
static bool parseNextInt(const char** cursor, int* out)
{
    const char* p = *cursor;
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    if (*p < '0' || *p > '9') return false;

    int value = 0;
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        p++;
    }

    *out = neg ? -value : value;
    if (*p == ',') p++;
    *cursor = p;
    return true;
}

static void handleRfCfgSet(const char* args)
{
    int power, sf, bw, cr;
    const char* p = args;
    if (!parseNextInt(&p, &power) || !parseNextInt(&p, &sf) ||
        !parseNextInt(&p, &bw) || !parseNextInt(&p, &cr)) {
        printf("ERROR\r\n");
        return;
    }

    if (power < RFCFG_POWER_MIN || power > RFCFG_POWER_MAX ||
        sf < RFCFG_SF_MIN || sf > RFCFG_SF_MAX ||
        bw < RFCFG_BW_MIN || bw > RFCFG_BW_MAX ||
        cr < RFCFG_CR_MIN || cr > RFCFG_CR_MAX) {
        printf("ERROR\r\n");
        return;
    }

    currentPower = (int8_t)power;
    currentSF    = (uint8_t)sf;
    currentBW    = (uint8_t)bw;
    currentCR    = (uint8_t)cr;
    applyRadioConfig();

    printf("OK\r\n");
}

static void handleRxModeSet(const char* args)
{
    int mode;
    const char* p = args;
    if (!parseNextInt(&p, &mode) ||
        (mode != RXMODE_TX_ONLY && mode != RXMODE_CONTINUOUS)) {
        printf("ERROR\r\n");
        return;
    }

    currentRxMode = (uint8_t)mode;
    rearmOrSleep(); // take effect immediately, not just on the next TX/RX event

    printf("OK\r\n");
}

static void handleCommand(const char* line)
{
    static const char txPrefix[]       = "AT+TEST=TXLRPKT,\"";
    static const char rfCfgSetPrefix[] = "AT+RFCFG=";
    static const char rfCfgQuery[]     = "AT+RFCFG?";
    static const char rxModeSetPrefix[] = "AT+RXMODE=";
    static const char rxModeQuery[]     = "AT+RXMODE?";

    if (strncmp(line, txPrefix, sizeof(txPrefix) - 1) == 0) {
        handleTxCommand(line, txPrefix, sizeof(txPrefix) - 1);
        return;
    }

    if (strcmp(line, rfCfgQuery) == 0) {
        printf("+RFCFG: %d,%d,%d,%d\r\n", (int)currentPower, (int)currentSF,
               (int)currentBW, (int)currentCR);
        printf("OK\r\n");
        return;
    }

    if (strncmp(line, rfCfgSetPrefix, sizeof(rfCfgSetPrefix) - 1) == 0) {
        handleRfCfgSet(line + sizeof(rfCfgSetPrefix) - 1);
        return;
    }

    if (strcmp(line, rxModeQuery) == 0) {
        printf("+RXMODE: %d\r\n", (int)currentRxMode);
        printf("OK\r\n");
        return;
    }

    if (strncmp(line, rxModeSetPrefix, sizeof(rxModeSetPrefix) - 1) == 0) {
        handleRxModeSet(line + sizeof(rxModeSetPrefix) - 1);
        return;
    }

    printf("ERROR\r\n");
}

int main(void)
{
    // UART0 bring-up, written inline and byte-identical to the proven-working
    // link_probe/uart_probe firmware, as the literal first thing main() does.
    rcc_enable_peripheral_clk(RCC_PERIPHERAL_UART0, true);
    rcc_enable_peripheral_clk(RCC_PERIPHERAL_GPIOB, true);

    gpio_set_iomux(GPIOB, GPIO_PIN_1, 1); // UART0_TX: GP17

    uart_config_t uart_cfg;
    uart_config_init(&uart_cfg);
    uart_cfg.baudrate = 9600;
    uart_init(CONFIG_DEBUG_UART, &uart_cfg);
    uart_cmd(CONFIG_DEBUG_UART, ENABLE);

    // Checkpoints below print once at boot. The host listener must already be
    // attached BEFORE the board is power-cycled to catch them - if the board
    // hangs partway through init, everything printed before the hang is gone
    // by the time a listener started afterwards connects.
    printf("\r\n=== p2p_bridge boot ===\r\n");

    board_init();
    printf("CKPT: board_init done\r\n");

    RadioEvents.TxDone    = OnTxDone;
    RadioEvents.RxDone    = OnRxDone;
    RadioEvents.TxTimeout = OnTxTimeout;
    RadioEvents.RxTimeout = OnRxTimeout;
    RadioEvents.RxError   = OnRxError;

    Radio.Init(&RadioEvents);
    printf("CKPT: radio init done\r\n");
    Radio.SetChannel(RF_FREQUENCY);
    printf("CKPT: set channel done\r\n");

    applyRadioConfig(); // applies DEFAULT_* values and starts Rx
    printf("CKPT: rf config applied (power=%d sf=%d bw=%d cr=%d)\r\n",
           (int)currentPower, (int)currentSF, (int)currentBW, (int)currentCR);

    printf("READY\r\n");

    while (1) {
        while (lpuart_get_rx_not_empty_status(LPUART)) {
            char c = (char)lpuart_receive_data(LPUART);
            if (c == '\n') {
                cmdBuf[cmdLen] = '\0';
                if (cmdLen > 0) {
                    handleCommand(cmdBuf);
                }
                cmdLen = 0;
            } else if (c != '\r' && cmdLen < CMD_BUF_LEN - 1) {
                cmdBuf[cmdLen++] = c;
            }
        }

        if (RxPending) {
            RxPending = false;
            char hexOut[MAX_PAYLOAD * 2 + 1];
            hexEncode(RxPayload, RxPayloadSize, hexOut);
            printf("+TEST: RXLRPKT,\"%s\",%d,%d\r\n", hexOut, LastRssi, LastSnr);
        }

        Radio.IrqProcess();
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(void* file, uint32_t line)
{
    (void)file;
    (void)line;
    while (1) { }
}
#endif
