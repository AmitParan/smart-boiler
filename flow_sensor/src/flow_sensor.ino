// ---------------------------------------------------------------
// SLAVE ESP32 — Flow Sensor + UART2 Communication
// ---------------------------------------------------------------

// UART2 pins (same as before)
#define RXD2 16
#define TXD2 17

// Flow sensor on GPIO2
#define SENSOR_PIN 2

volatile uint32_t pulseCount = 0;
unsigned long lastTime = 0;
const float K_FACTOR = 6.6;

String receivedData = "";   // optional RX buffer

// Interrupt: count a pulse
void IRAM_ATTR pulseISR() {
  pulseCount++;
}

void setup() {
  Serial.begin(115200);
  Serial.println("SLAVE READY (Flow + UART2)");

  // UART2 setup
  Serial2.begin(115200, SERIAL_8N1, RXD2, TXD2);

  // Flow sensor input
  pinMode(SENSOR_PIN, INPUT);  
  // You can try INPUT_PULLDOWN if needed:
  // pinMode(SENSOR_PIN, INPUT_PULLDOWN);

  // Interrupt on RISING edge
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), pulseISR, RISING);

  lastTime = millis();
}

void loop() {

  // ----------------------------------------------------------
  // 1) SEND FLOW SENSOR DATA EVERY 1 SECOND
  // ----------------------------------------------------------
  if (millis() - lastTime >= 1000) {

    noInterrupts();
    uint32_t pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    float flow_Lmin = pulses / K_FACTOR;

    // Prepare message for MASTER
    String msg = "FLOW:" + String(flow_Lmin, 2) + "\n";

    // Send to MASTER
    Serial2.print(msg);

    // Debug
    Serial.print("SLAVE TX → ");
    Serial.print(msg);

    lastTime = millis();
  }

  // ----------------------------------------------------------
  // 2) RECEIVE COMMANDS FROM MASTER (optional)
  // ----------------------------------------------------------
  while (Serial2.available()) {
    char c = Serial2.read();

    if (c == '\n') {
      Serial.print("SLAVE RX ← ");
      Serial.println(receivedData);
      receivedData = "";
    }
    else {
      receivedData += c;
    }
  }
}
