// Arduino libraries
#include <SPI.h> 

// Custom
#include "SoftVCNL4040.h"
#include "Serial7Seg.h"

//*****************************************************************************
// Settings

const uint16_t TRIGGER_THRESHOLD = 300;
const uint8_t DISPLAY_SS_PIN = 8;
const uint8_t DISPLAY_BRIGHTNESS = 255;

//*****************************************************************************
// Globals

// Two sensors sharing one SCL line, each with its own SDA pin. They all
// live at I2C address 0x60, so giving each its own SDA line is how we
// address them independently.
SoftVCNL4040 prox1(2, 5);  // SDA 2, shared SCL 5
SoftVCNL4040 prox2(4, 5);  // SDA 4, shared SCL 5

// 7-segment LED display
Serial7Seg display(DISPLAY_SS_PIN);

//*****************************************************************************
// Functions

void reportTrigger(uint8_t lane, uint16_t reading) {
  Serial.print(F("TRIGGER lane "));
  Serial.print(lane);
  Serial.print(F(" @ "));
  Serial.print(millis());
  Serial.print(F(" ms  (reading: "));
  Serial.print(reading);
  Serial.println(')');
}

//*****************************************************************************
// Main

void setup() {

  // Pour a bowl of serial
  Serial.begin(115200);
  Serial.println(F("Booting..."));

  // Initialize the display
  display.begin();
  display.clear();
  display.setBrightness(DISPLAY_BRIGHTNESS);

  // Initialize proximity sensors
  bool ok1 = prox1.begin();
  bool ok2 = prox2.begin();
  if (!ok1) {
    Serial.println(F("Sensor 1 not found"));
  }
  if (!ok2) {
    Serial.println(F("Sensor 2 not found"));
  }
  if (!ok1 || !ok2) {
    while(1);
  }
  Serial.println(F("Both sensors armed"));
}

void loop() {
  bool trigger_1 = false;
  static bool trigger_1_prev = false;
  static bool timer_running = false;
  static unsigned long timer = 0;
  static unsigned long start_time = 0;

  // Start timer on proximity 1 trigger
  trigger_1 = prox1.checkTrigger(TRIGGER_THRESHOLD);
  if (trigger_1 && !trigger_1_prev) {
    Serial.println("Start");
    display.setDecimals(Serial7Seg::DP2);
    timer = 0;
    timer_running = true;
    start_time = millis();
  }
  trigger_1_prev = trigger_1;

  // Update counter
  if (timer_running) {
    timer = millis() - start_time;
    display.print((int)(timer / 10));
  }
}
