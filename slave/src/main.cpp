// =============================================================================
//  PING TEST BRANCH — SLAVE (ESP32-C6)   [branch: ping-test]
//  Raw-UART link test. No FreeRTOS, no protocol, no CRC, no sensors.
//  Restore the real firmware with:  git checkout v9_merge
//
//  Serial1: TX=GPIO11 -> KQ-330 modem RX,  RX=GPIO10 <- KQ-330 modem TX, 9600.
//  Counts raw bytes/5s, prints assembled lines, replies PONG.
//
//  READING IT:
//   * "[SLAVE GOT] \"PING123\"" clean  -> baud OK, corruption is elsewhere.
//   * Garbage bytes instead of PING    -> baud/toolchain mismatch confirmed.
//   * raw bytes == 0                    -> nothing crosses (shouldn't happen now).
// =============================================================================
#include <Arduino.h>

#define PLC_TX_PIN 11
#define PLC_RX_PIN 10
#define PLC_BAUD   9600

static uint32_t last_report = 0;
static uint32_t raw_bytes   = 0;
static char     line_buf[64];
static uint8_t  line_len    = 0;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== SLAVE PING TEST ===");
    Serial1.begin(PLC_BAUD, SERIAL_8N1, PLC_RX_PIN, PLC_TX_PIN);
    Serial.printf("[SLAVE PING] Serial1 up: TX=GPIO%d RX=GPIO%d @ %d baud\n",
                  PLC_TX_PIN, PLC_RX_PIN, PLC_BAUD);
    last_report = millis();
}

void loop() {
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        raw_bytes++;

        // Show every raw byte so corruption is visible directly
        Serial.printf("  byte 0x%02X '%c'\n", (uint8_t)c,
                      (c >= 32 && c < 127) ? c : '.');

        if (c == '\n' || line_len >= sizeof(line_buf) - 1) {
            line_buf[line_len] = '\0';
            if (line_len > 0) {
                Serial.printf("[SLAVE GOT] \"%s\"  -> replying PONG\n", line_buf);
                Serial1.printf("PONG\n");
            }
            line_len = 0;
        } else if (c >= 32 && c < 127) {
            line_buf[line_len++] = c;
        }
    }

    if (millis() - last_report >= 5000UL) {
        Serial.printf("[SLAVE PING] raw bytes seen in last 5s: %lu\n",
                      (unsigned long)raw_bytes);
        raw_bytes   = 0;
        last_report = millis();
    }
}
