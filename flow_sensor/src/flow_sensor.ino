#define SENSOR_PIN 14   // Flow sensor yellow wire with divider

volatile uint32_t pulseCount = 0;
unsigned long lastTime = 0;

void IRAM_ATTR pulseISR() {
  pulseCount++;
}

void setup() {
  Serial.begin(115200);

  // Because you are using a resistor divider, DO NOT use INPUT_PULLUP.
  pinMode(SENSOR_PIN, INPUT);

  // With a divider, we detect the rising edge when the sensor pulls low.
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), pulseISR, RISING);

  lastTime = millis();
}

void loop() {
  if (millis() - lastTime >= 1000) {  // every 1 second

    noInterrupts();
    uint32_t pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    // YF-B6 calibration — You can tune later
    // Flow (L/min) = pulses / 6.6
    float flow_Lmin = pulses / 6.6;

    Serial.print("Pulses: ");
    Serial.print(pulses);
    Serial.print("   Flow: ");
    Serial.print(flow_Lmin);
    Serial.println(" L/min");

    lastTime = millis();
  }
}
