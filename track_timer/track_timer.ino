#include "SoftVCNL4040.h"

// Two sensors sharing one SCL line, each with its own SDA pin. They all
// live at I2C address 0x60, so giving each its own SDA line is how we
// address them independently.
SoftVCNL4040 prox1(2, 5);  // SDA 2, shared SCL 5
SoftVCNL4040 prox2(4, 5);  // SDA 4, shared SCL 5

const uint16_t TRIGGER_THRESHOLD = 300;

void setup() {
  Serial.begin(115200);
  Serial.println(F("booting..."));

  bool ok1 = prox1.begin();
  bool ok2 = prox2.begin();
  if (!ok1) Serial.println(F("Sensor 1 not found"));
  if (!ok2) Serial.println(F("Sensor 2 not found"));
  if (!ok1 || !ok2) while (1);

  Serial.println(F("Both sensors armed"));
}

void reportTrigger(uint8_t lane, uint16_t reading) {
  Serial.print(F("TRIGGER lane "));
  Serial.print(lane);
  Serial.print(F(" @ "));
  Serial.print(millis());
  Serial.print(F(" ms  (reading: "));
  Serial.print(reading);
  Serial.println(')');
}

void loop() {
  // One read per sensor per loop. checkTrigger() caches the value, so
  // last() below reuses it instead of hitting the bus again.
  if (prox1.checkTrigger(TRIGGER_THRESHOLD)) reportTrigger(1, prox1.last());
  if (prox2.checkTrigger(TRIGGER_THRESHOLD)) reportTrigger(2, prox2.last());

  // Periodic raw dump, reusing the values we already read this loop.
  static uint32_t lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    lastPrint = millis();
    Serial.print(F("raw: "));
    Serial.print(prox1.last());
    Serial.print('\t');
    Serial.println(prox2.last());
  }
  // no delay -- poll as fast as possible for timing accuracy
}
