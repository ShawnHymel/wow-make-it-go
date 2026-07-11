// sensors_test_hwi2c -- two-sensor diagnostic, MIXED bus version.
//
// Same idea as sensors_test.ino, but the two sensors sit on DIFFERENT buses:
//   Sensor 1 (start gate):  BIT-BANGED software I2C (SoftVCNL4040) on pins 2/5
//   Sensor 2 (finish line): HARDWARE I2C (HardVCNL4040, the Wire library) on A4/A5
//
// Both VCNL4040s share address 0x60, so they can't live on the same bus. Here
// they don't have to: sensor 1 has its own bit-banged SDA/SCL, and sensor 2
// owns the dedicated hardware TWI pins. One sensor per bus, no address clash.
//
// Wiring:
//   Both sensors: RED->3.3V (NOT 5V), BLACK->GND
//   Sensor 1 (start, bit-banged):  BLUE->SDA = pin 2,  YELLOW->SCL = pin 5
//   Sensor 2 (finish, hardware):   BLUE->SDA = A4,     YELLOW->SCL = A5
//
// Sensor 1 keeps its wiring from the all-bit-bang test (SDA 2 / SCL 5).
// A4/A5 are the ATmega328's fixed hardware-I2C pins -- only sensor 2 moves
// there (it was on pins 4/5 before).
//
// Open the Serial Monitor at 115200 baud.

#include "SoftVCNL4040.h"
#include "HardVCNL4040.h"

// --- Sensor 1 pins (bit-banged; independent of the hardware bus) ------------
const uint8_t SDA1_PIN = 2;   // sensor 1 data  (BLUE)
const uint8_t SCL1_PIN = 5;   // sensor 1 clock (YELLOW) -- same as all-bit-bang test
// Sensor 2 has no pin constants: hardware I2C is fixed at A4 (SDA) / A5 (SCL).

// Same threshold the real sketch uses, so covered/clear here matches the timer.
const uint16_t TRIGGER_THRESHOLD = 300;

// How often to print (ms). Slow enough to read, fast enough to feel live.
const unsigned long PRINT_INTERVAL_MS = 100;

SoftVCNL4040 prox1(SDA1_PIN, SCL1_PIN);   // start gate  -- software bus
HardVCNL4040 prox2;                        // finish line -- hardware bus (A4/A5)

// Per-sensor running stats, handy for spotting noise / drift / dropouts.
struct Stats {
  uint16_t minReading = 0xFFFF;
  uint16_t maxReading = 0;
  unsigned long errorCount = 0;
};
Stats stats1, stats2;
unsigned long sampleCount = 0;

// Update this sensor's stats from its latest reading.
void accumulate(Stats& s, bool ok, uint16_t reading) {
  if (!ok) { s.errorCount++; return; }
  if (reading < s.minReading) s.minReading = reading;
  if (reading > s.maxReading) s.maxReading = reading;
}

// Print one sensor's block: reading, ok flag, covered/clear state. Takes the
// already-extracted values so it doesn't care which driver produced them.
void printSensor(bool ok, uint16_t reading, bool covered) {
  if (ok) {
    Serial.print(reading);
    Serial.print(reading < 10 ? F("     ") :
                 reading < 100 ? F("    ")  :
                 reading < 1000 ? F("   ")  :
                 reading < 10000 ? F("  ")  : F(" "));
    Serial.print(F("yes  "));
  } else {
    // 0xFFFF sentinel from read16() -> I2C NAK / no sensor / bad cable.
    Serial.print(F("--    no   "));
  }
  Serial.print(covered ? F("COVERED") : F("clear  "));
}

// Print a sensor's running stats: min/max reading and errors/samples.
void printStats(const Stats& s) {
  if (s.minReading == 0xFFFF) Serial.print(F("--/"));
  else { Serial.print(s.minReading); Serial.print('/'); }
  Serial.print(s.maxReading);
  Serial.print(F(" e"));
  Serial.print(s.errorCount);
  Serial.print('/');
  Serial.print(sampleCount);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) { /* wait for USB serial on boards that need it */ }

  Serial.println();
  Serial.println(F("=== Two-sensor diagnostic (mixed bus) ==="));
  Serial.println(F("Sensor 1 (start):  bit-banged software I2C"));
  Serial.print  (F("                   SDA pin ")); Serial.print(SDA1_PIN);
  Serial.print  (F(", SCL pin ")); Serial.println(SCL1_PIN);
  Serial.println(F("Sensor 2 (finish): hardware I2C on A4 (SDA) / A5 (SCL)"));
  Serial.print  (F("Trigger threshold: ")); Serial.println(TRIGGER_THRESHOLD);
  Serial.println();

  Serial.println(F("Calling begin()..."));

  Serial.print(F("  Sensor 1 (bit-bang pins "));
  Serial.print(SDA1_PIN); Serial.print('/'); Serial.print(SCL1_PIN); Serial.print(F("): "));
  Serial.println(prox1.begin() ? F("OK -- found (ID 0x0186 matched).")
                               : F("FAILED -- did not answer."));

  Serial.print(F("  Sensor 2 (hardware A4/A5): "));
  Serial.println(prox2.begin() ? F("OK -- found (ID 0x0186 matched).")
                               : F("FAILED -- did not answer."));

  Serial.println();
  Serial.println(F("If sensor 1 FAILED: check RED->3.3V (not 5V), BLACK->GND,"));
  Serial.print  (F("BLUE->pin ")); Serial.print(SDA1_PIN);
  Serial.print  (F(", YELLOW->pin ")); Serial.println(SCL1_PIN);
  Serial.println(F("If sensor 2 FAILED: check RED->3.3V, BLACK->GND,"));
  Serial.println(F("BLUE->A4, YELLOW->A5 (it moved off pins 4/5 for hardware I2C)."));
  Serial.println(F("Polling continues below so you can wiggle a cable and watch"));
  Serial.println(F("it come alive."));
  Serial.println();

  // Column header. Each sensor block: reading / ok / state.
  Serial.println(F("---- Sensor 1 (gate) ----   ---- Sensor 2 (finish) --   stats (min/max/err)"));
  Serial.println(F("read   ok   state           read   ok   state          S1              S2"));
}

void loop() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint < PRINT_INTERVAL_MS) return;
  lastPrint = millis();

  // Poll both sensors once. checkTrigger() reads the sensor and refreshes
  // ok()/covered(); we ignore the edge return and just report the numbers.
  prox1.checkTrigger(TRIGGER_THRESHOLD);
  prox2.checkTrigger(TRIGGER_THRESHOLD);

  sampleCount++;
  accumulate(stats1, prox1.ok(), prox1.last());
  accumulate(stats2, prox2.ok(), prox2.last());

  // --- one aligned line per sample ---
  printSensor(prox1.ok(), prox1.last(), prox1.covered());
  Serial.print(F("   "));
  printSensor(prox2.ok(), prox2.last(), prox2.covered());
  Serial.print(F("   "));

  // Trailing stats: min/max observed and error tally per sensor.
  printStats(stats1);
  Serial.print(F("   "));
  printStats(stats2);
  Serial.println();
}
