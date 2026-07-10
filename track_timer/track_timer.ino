// Arduino libraries
#include <SPI.h>
#include <avr/wdt.h>

// Custom libraries
#include "SoftVCNL4040.h"
#include "HardVCNL4040.h"
#include "Serial7Seg.h"

//*****************************************************************************
// Settings

const uint16_t TRIGGER_DELTA     = 500;   // how far above ambient = "covered"
const uint8_t  AMBIENT_SAMPLES   = 10;    // readings to average during calibration
const uint8_t  DISPLAY_SS_PIN    = 8;
const uint8_t  DISPLAY_BRIGHTNESS = 255;

// A run that never trips the finish line is abandoned here. The timer
// freezes at the ceiling and the display returns to READY.
const unsigned long RACE_TIMEOUT_MS = 999900UL;   // 999.9 s

// Finish-line time flash: blink the result this many times before holding.
const uint8_t      FINISH_FLASH_COUNT = 4;
const unsigned long FINISH_FLASH_MS   = 300UL;

// Consecutive read failures before a hard reboot.
const uint8_t ERROR_REBOOT_LIMIT = 10;

// Watchdog timeout. A normal loop() pass is a few milliseconds -- two sensor
// reads and at most one SPI write -- so a second leaves enormous headroom
// while still recovering faster than a spectator can register the freeze.
const uint8_t WDT_TIMEOUT = WDTO_1S;

//*****************************************************************************
// Globals

// Both VCNL4040s answer to I2C address 0x60, so they can't share a bus.
// We split them across two buses instead: the start gate is bit-banged on
// its own SDA/SCL pins, and the finish line owns the ATmega's dedicated
// hardware-I2C pins (A4 = SDA, A5 = SCL). One sensor per bus, no clash.
SoftVCNL4040 prox1(2, 5);  // bit-banged SDA 2 / SCL 5 -- start gate
HardVCNL4040 prox2;         // hardware I2C on A4/A5    -- finish line

// 7-segment LED display
Serial7Seg display(DISPLAY_SS_PIN);

// Proximity sensor ambient baselines (captured during setup)
uint16_t ambient1 = 0;
uint16_t ambient2 = 0;


//*****************************************************************************
// Timer state machine
//
//   READY    -- idle. Display holds 0.0 (boot) or the last race time until a
//               new object is placed on the start gate.
//   PRIMED   -- object resting on the gate. Show steady "GO" until released.
//   RUNNING  -- object has left the gate; count up live. Stop on the finish
//               line (sensor 2) or at the timeout ceiling.
//   FINISHED -- finish line tripped; flash the final time a few times, then
//               hold it and return to READY.
//   ERROR    -- a sensor read failed; show Err1/Err2 until it recovers.
//
// Nothing here blocks: every transition is driven by millis() and the
// sensors' own edge/level state.

enum State { READY, PRIMED, RUNNING, FINISHED, ERROR };

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

// Force a full hardware reset. Arming the watchdog for its shortest timeout
// and spinning lets it fire, which resets the whole MCU (registers,
// peripherals and all) -- unlike a jump-to-zero soft reset. Give the serial
// line a moment to flush the message first.
void hardReboot() {
  Serial.flush();
  wdt_enable(WDTO_15MS);
  while (1) { }
}

// Read N samples from each sensor and store the average as the ambient
// baseline. Called once during setup after both sensors are initialised.
void calibrateAmbient() {
  uint32_t sum1 = 0, sum2 = 0;
  for (uint8_t i = 0; i < AMBIENT_SAMPLES; i++) {
    sum1 += prox1.readProximity();
    sum2 += prox2.readProximity();
    delay(50);
  }
  ambient1 = sum1 / AMBIENT_SAMPLES;
  ambient2 = sum2 / AMBIENT_SAMPLES;

  Serial.print(F("Ambient 1: ")); Serial.println(ambient1);
  Serial.print(F("Ambient 2: ")); Serial.println(ambient2);
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

//*****************************************************************************
// Main

void setup() {

  // Disarm the watchdog before anything else. Coming out of a watchdog reset
  // the WDT is still armed at the timeout that fired it, and WDRF must be
  // cleared before WDE can be -- clearing WDE alone is a silent no-op.
  // Optiboot already does this before handing us control (which is why the
  // existing hardReboot() recovers instead of bricking the board); this is
  // belt-and-braces, and it will NOT rescue a stock bootloader -- that one
  // gets reset inside its own startup delay, long before setup() runs.
  MCUSR &= ~_BV(WDRF);
  wdt_disable();

  // Pour a bowl of serial
  Serial.begin(115200);

  // Initialize the display
  Serial.println(F("Initializing display..."));
  display.begin();
  display.clear();
  display.setBrightness(DISPLAY_BRIGHTNESS);

  // Initialize start proximity sensor
  Serial.println(F("Initializing proximity sensor 1..."));
  delay(1000);
  bool ok1 = prox1.begin();
  if (!ok1) {
    Serial.println(F("Error: Sensor 1 not found"));
    showText("Err1");
    delay(1000);
    hardReboot();
  }

  // Initialize finish proximity sensor
  Serial.println(F("Initializing proximity sensor 2..."));
  delay(1000);
  bool ok2 = prox2.begin();
  if (!ok2) {
    Serial.println(F("Error: Sensor 2 not found"));
    showText("Err2");
    delay(1000);
    hardReboot();
  }

  // Capture ambient proximity baselines before anything is on the track.
  Serial.println(F("Both sensors armed. Calibrating..."));
  calibrateAmbient();

  // Timer starts at 0.0, waiting for an object on the gate.
  showTenths(0);

  // Arm the watchdog last. Everything above blocks for well over two seconds
  // (two settle delays plus AMBIENT_SAMPLES * 50 ms of calibration), so any
  // timeout short enough to be useful in loop() would reset us mid-setup.
  Serial.println(F("Enabling watchdog timer..."));
  wdt_enable(WDT_TIMEOUT);

  // Say we've booted
  Serial.println(F("Boot complete. Running!"));
}

void loop() {
  static State state = READY;
  static unsigned long startTime    = 0;   // millis() when the gate released
  static unsigned long finishTenths = 0;   // cached finish time for flashing
  static unsigned long flashClock   = 0;   // last finish blink toggle
  static uint8_t       flashesLeft  = 0;   // remaining finish blinks
  static bool          flashOn      = false;
  static bool          prevCovered1 = false;

  // Feed the dog at the TOP of loop(), not the bottom: the error branch below
  // returns early, and a feed at the bottom would be skipped on every failed
  // read -- handing the watchdog a reboot that ERROR_REBOOT_LIMIT already owns.
  wdt_reset();

  // Poll both sensors once per pass. checkTrigger() also refreshes each
  // sensor's ok()/covered() state.
  bool rise1 = prox1.checkTrigger(ambient1 + TRIGGER_DELTA);   // object arrived at gate
  bool rise2 = prox2.checkTrigger(ambient2 + TRIGGER_DELTA);   // object hit finish line

  // A bad read trumps everything: show the error and skip the rest of
  // loop() until the sensor recovers. Reboot after too many in a row.
  static uint8_t errCount1 = 0;
  static uint8_t errCount2 = 0;

  bool fail1 = !prox1.ok();
  bool fail2 = !prox2.ok();

  // Error recovery for sensors: show error code and reboot if the error occurs
  // too many times.
  if (fail1 || fail2) {
    if (fail1) {
      errCount1++;
      Serial.print(F("Error: sensor 1 ("));
      Serial.print(errCount1);
      Serial.println(')');
    }
    if (fail2) {
      errCount2++;
      Serial.print(F("Error: sensor 2 ("));
      Serial.print(errCount2);
      Serial.println(')');
    }
    // Show whichever sensor is failing (prefer Err1 if both)
    showText(fail1 ? "Err1" : "Err2");
    state = ERROR;

    if (errCount1 >= ERROR_REBOOT_LIMIT || errCount2 >= ERROR_REBOOT_LIMIT) {
      Serial.println(F("Too many consecutive errors -- rebooting"));
      hardReboot();
    }
    return;   // skip the rest of loop() until next pass
  }

  // Both sensors read OK this pass.
  if (state == ERROR) {
    errCount1 = 0;
    errCount2 = 0;
    state = READY;
    prevCovered1 = prox1.covered();  // resync so we don't fire a phantom edge
    showTenths(0);
  }
  errCount1 = 0;
  errCount2 = 0;

  // Falling edge on the start gate = the object was released.
  bool covered1 = prox1.covered();
  bool release1 = prevCovered1 && !covered1;
  prevCovered1 = covered1;

  // A rising edge on the start gate always primes, regardless of state.
  // This lets a runner re-stage mid-race without waiting for a timeout.
  if (rise1 && state != PRIMED) {
    reportTrigger(1, prox1.last());
    Serial.println(F("PRIMED -- GO"));
    state = PRIMED;
    display.clear();
    showText("-GO-");
  }

  switch (state) {

    case READY:
      break;   // waiting for rise1, handled above

    case PRIMED:
      if (release1) {
        Serial.println(F("START"));
        state = RUNNING;
        startTime = millis();
        display.clear();
        showTenths(0);
      }
      break;

    case RUNNING: {
      unsigned long elapsed = millis() - startTime;
      bool timedOut = elapsed >= RACE_TIMEOUT_MS;
      if (timedOut) elapsed = RACE_TIMEOUT_MS;   // clamp to the ceiling
      showTenths(elapsed / 100);

      if (rise2) {
        reportTrigger(2, prox2.last());
        Serial.print(F("FINISH @ "));
        Serial.print(elapsed / 1000.0, 1);
        Serial.println(F(" s"));
        finishTenths = elapsed / 100;
        flashClock   = millis();
        flashesLeft  = FINISH_FLASH_COUNT;
        flashOn      = false;              // start with blank (first toggle shows time)
        showText("    ");
        state = FINISHED;
      } else if (timedOut) {
        Serial.print(F("TIMEOUT @ "));
        Serial.print(elapsed / 1000.0, 1);
        Serial.println(F(" s"));
        state = READY;                     // hold steady, no flash
      }
      break;
    }

    case FINISHED:
      if (flashesLeft > 0) {
        if (millis() - flashClock >= FINISH_FLASH_MS) {
          flashClock = millis();
          flashOn = !flashOn;
          if (flashOn) {
            showTenths(finishTenths);
          } else {
            showText("    ");
            flashesLeft--;
          }
        }
      } else {
        showTenths(finishTenths);           // hold the final time
        state = READY;
      }
      break;

    case ERROR:
      break;   // handled above; here to keep the compiler happy
  }
}
