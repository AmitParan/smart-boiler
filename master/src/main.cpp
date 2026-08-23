// =============================================================================
//  PING TEST BRANCH — MASTER (ESP32-S3)   [branch: ping-test]
//  Raw-UART link test. No FreeRTOS, no protocol, no CRC, no LVGL.
//  Restore the real firmware with:  git checkout v9_merge
//
//  Serial1: TX=GPIO17 -> KQ-330 modem RX,  RX=GPIO13 <- KQ-330 modem TX, 9600.
//  Prints "sent PING #N" once/sec and dumps any byte received (hex + char).
// =============================================================================
#include <Arduino.h>

#define PLC_TX_PIN 17
#define PLC_RX_PIN 13
#define PLC_BAUD   9600

static uint32_t last_send = 0;
static uint32_t ping_num  = 0;

void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("=== MASTER PING TEST ===");
    Serial1.begin(PLC_BAUD, SERIAL_8N1, PLC_RX_PIN, PLC_TX_PIN);
    Serial.printf("[MASTER PING] Serial1 up: TX=GPIO%d RX=GPIO%d @ %d baud\n",
                  PLC_TX_PIN, PLC_RX_PIN, PLC_BAUD);
}

void loop() {
    if (millis() - last_send >= 1000UL) {
        last_send = millis();
        ping_num++;
        Serial1.printf("PING%lu\n", (unsigned long)ping_num);
        Serial.printf("[MASTER PING] sent PING #%lu\n", (unsigned long)ping_num);
    }

    while (Serial1.available()) {
        char c = (char)Serial1.read();
        Serial.printf("[MASTER GOT] 0x%02X '%c'\n", (uint8_t)c,
                      (c >= 32 && c < 127) ? c : '.');
    }
}
