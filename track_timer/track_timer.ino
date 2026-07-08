// Arduino libraries
#include <SPI.h>

// Custom
#include "SoftVCNL4040.h"
#include "Serial7Seg.h"

//*****************************************************************************
// Settings

const uint16_t TRIGGER_THRESHOLD = 300;   // proximity reading counted as "covered"
const uint8_t  DISPLAY_SS_PIN    = 8;
const uint8_t  DISPLAY_BRIGHTNESS = 255;

// A run that never trips the finish line is abandoned here. The timer
// freezes at the ceiling and the display returns to READY.
const unsigned long RACE_TIMEOUT_MS = 99900UL;   // 99.9 s

// How fast the "GO" prompt blinks while the gate is primed.
const unsigned long FLASH_INTERVAL_MS = 400UL;

//*****************************************************************************
// Globals

// Two sensors sharing one SCL line, each with its own SDA pin. They all
// live at I2C address 0x60, so giving each its own SDA line is how we
// address them independently.
SoftVCNL4040 prox1(2, 5);  // SDA 2, shared SCL 5 -- start gate
SoftVCNL4040 prox2(4, 5);  // SDA 4, shared SCL 5 -- finish line

// 7-segment LED display
Serial7Seg display(DISPLAY_SS_PIN);

//*****************************************************************************
// Timer state machine
//
//   READY   -- idle. Display holds 0.0 (boot) or the last race time until a
//              new object is placed on the start gate.
//   PRIMED  -- object resting on the gate. Flash "GO" until it is released.
//   RUNNING -- object has left the gate; count up live. Stop on the finish
//              line (sensor 2) or at the timeout ceiling.
//   ERROR   -- a sensor read failed; show Err1/Err2 until it recovers.
//
// Nothing here blocks: every transition is driven by millis() and the
// sensors' own edge/level state.

enum State { READY, PRIMED, RUNNING, ERROR };

//*****************************************************************************
// Display helpers
//
// The display keeps whatever it was last sent, so we only push over SPI
// when the shown content actually changes. showMode tracks whether the
// display is currently rendering a number (with the SSS.C decimal point)
// or free text (GO / errors / blank), so we know when to toggle the
// decimal point and when a cached numeric value is still valid.

enum ShowMode { SHOW_NONE, SHOW_NUM, SHOW_TEXT };
ShowMode showMode = SHOW_NONE;

// Show a time as SSS.C (seconds with one tenth). The ones-of-seconds
// digit is always drawn, so zero reads as "0.0" rather than ".0".
void showTenths(unsigned long tenths) {
  static long lastNum = -1;
  if (showMode != SHOW_NUM) {
    display.setDecimals(Serial7Seg::DP3);   // decimal point after digit 3
    showMode = SHOW_NUM;
    lastNum = -1;                            // force a redraw after text
  }
  if ((long)tenths == lastNum) return;
  lastNum = (long)tenths;

  char buf[8];
  snprintf(buf, sizeof(buf), "%3lu%1lu", tenths / 10, tenths % 10);
  display.print(buf);
}

// Show up to four characters with the decimal point off. Called only on
// state changes and blink toggles, so it just resends every time.
void showText(const char* s) {
  if (showMode != SHOW_TEXT) {
    display.setDecimals(0);
    showMode = SHOW_TEXT;
  }
  display.print(s);
}

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

  // Timer starts at 0.0, waiting for an object on the gate.
  showTenths(0);
}

void loop() {
  static State state = READY;
  static unsigned long startTime  = 0;   // millis() when the gate released
  static unsigned long flashClock = 0;   // last GO blink toggle
  static bool          flashOn    = false;
  static bool          prevCovered1 = false;

  // Poll both sensors once per pass. checkTrigger() also refreshes each
  // sensor's ok()/covered() state.
  bool rise1 = prox1.checkTrigger(TRIGGER_THRESHOLD);   // object arrived at gate
  bool rise2 = prox2.checkTrigger(TRIGGER_THRESHOLD);   // object hit finish line

  // A bad read trumps everything: surface it and hold until it recovers.
  if (!prox1.ok() || !prox2.ok()) {
    showText(!prox1.ok() ? "Err1" : "Err2");
    state = ERROR;
    return;
  }
  if (state == ERROR) {              // reads recovered -> clean slate at 0.0
    state = READY;
    prevCovered1 = prox1.covered();  // resync so we don't fire a phantom edge
    showTenths(0);
  }

  // Falling edge on the start gate = the object was released.
  bool covered1 = prox1.covered();
  bool release1 = prevCovered1 && !covered1;
  prevCovered1 = covered1;

  switch (state) {

    case READY:
      // Display holds 0.0 or the previous result. Wait for the gate.
      if (rise1) {
        reportTrigger(1, prox1.last());
        Serial.println(F("PRIMED -- GO"));
        state = PRIMED;
        flashClock = millis();
        flashOn = true;
        showText(" GO ");
      }
      break;

    case PRIMED:
      if (release1) {
        Serial.println(F("START"));
        state = RUNNING;
        startTime = millis();
        showTenths(0);
      } else if (millis() - flashClock >= FLASH_INTERVAL_MS) {
        flashClock = millis();
        flashOn = !flashOn;
        showText(flashOn ? " GO " : "    ");
      }
      break;

    case RUNNING: {
      unsigned long elapsed = millis() - startTime;
      bool timedOut = elapsed >= RACE_TIMEOUT_MS;
      if (timedOut) elapsed = RACE_TIMEOUT_MS;   // clamp to the 99.9 s ceiling
      showTenths(elapsed / 100);

      if (rise2 || timedOut) {
        if (rise2) reportTrigger(2, prox2.last());
        Serial.print(timedOut ? F("TIMEOUT @ ") : F("FINISH @ "));
        Serial.print(elapsed / 100.0, 1);
        Serial.println(F(" s"));
        state = READY;   // freeze: the final time stays on the display
      }
      break;
    }

    case ERROR:
      break;   // handled above; here to keep the compiler happy
  }
}
