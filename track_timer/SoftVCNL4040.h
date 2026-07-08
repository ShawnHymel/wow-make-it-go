#ifndef SOFT_VCNL4040_H
#define SOFT_VCNL4040_H

// SoftVCNL4040 -- bit-banged (software) I2C driver for the VCNL4040
// proximity sensor. Header-only: just drop this next to your sketch and
// #include "SoftVCNL4040.h".
//
// Target: Arduino Uno / SparkFun RedBoard (5V AVR)
//
// WIRING (Qwiic cable colors):
//   RED    -> 3.3V   (NOT 5V -- the VCNL4040 is a 3.3V part)
//   BLACK  -> GND
//   BLUE   -> sdaPin (any digital pin)
//   YELLOW -> sclPin (any digital pin)
//
// No external pull-up resistors needed: the SparkFun breakout has
// 2.2k pull-ups to 3.3V on board. This code emulates open-drain --
// pins are only ever driven LOW or released to float, and internal
// pull-ups are never enabled, so the bus never sees 5V.
//
// Because pins are arbitrary, you can run multiple VCNL4040s (which all
// share address 0x60) by giving each its own SDA pin. Point them at a
// shared SCL pin and talk to one at a time; the idle sensors just sit on
// their own floating SDA line. See the accompanying .ino for a
// two-sensor example.

#include <Arduino.h>

class SoftVCNL4040 {
public:
  SoftVCNL4040(uint8_t sdaPin, uint8_t sclPin) : _sda(sdaPin), _scl(sclPin) {}

  bool begin() {
    // Release both lines. digitalWrite(LOW) while in INPUT mode clears
    // the PORT bit, which (a) keeps the internal pull-up OFF and
    // (b) means switching to OUTPUT always drives LOW.
    pinMode(_sda, INPUT);
    pinMode(_scl, INPUT);
    digitalWrite(_sda, LOW);
    digitalWrite(_scl, LOW);
    tick();

    if (read16(REG_ID) != 0x0186) return false;  // sensor not found

    // PS_CONF1/2: duty 1/40, integration time 8T, sensor ON, 16-bit output
    //   LSB 0x0E = PS_IT(3:1)=111, PS_SD(0)=0
    //   MSB 0x08 = PS_HD(3)=1 (16-bit proximity data)
    write16(REG_PS_CONF12, 0x080E);

    // PS_CONF3/MS: defaults + LED current 200 mA (LED_I(2:0)=111 in MSB)
    write16(REG_PS_CONF3MS, 0x0700);

    return true;
  }

  // Raw 16-bit proximity reading. Also caches the value; see last().
  uint16_t readProximity() {
    _last = read16(REG_PS_DATA);
    return _last;
  }

  // Sentinel returned by the register reads on an I2C failure (NAK / no
  // sensor). Proximity data never legitimately reaches 0xFFFF with these
  // settings, so it doubles as an "is this reading trustworthy" flag.
  static constexpr uint16_t READ_ERROR = 0xFFFF;

  // The most recent value read by readProximity() or checkTrigger().
  uint16_t last() const { return _last; }

  // True if the last read succeeded. A loose Qwiic cable or dead sensor
  // makes read16() return READ_ERROR; callers use this to surface an
  // error instead of acting on garbage.
  bool ok() const { return _last != READ_ERROR; }

  // Current debounced presence state: true while an object is over the
  // sensor, false once it has pulled back past the re-arm threshold.
  // Lets callers detect the *falling* edge (object leaving) as well as
  // the rising edge that checkTrigger() returns.
  bool covered() const { return _covered; }

  // Rising-edge trigger with hysteresis. Reads the sensor once (updating
  // last()) and returns true exactly once each time proximity crosses
  // above `threshold`. It re-arms only after the reading falls back below
  // (threshold - hysteresis), so a hovering hand won't machine-gun
  // triggers. This replaces the external covered1/covered2 bookkeeping --
  // each sensor now tracks its own state.
  //
  // A failed read (READ_ERROR) is never treated as a trigger and leaves
  // the covered state untouched -- otherwise 0xFFFF would read as "way
  // above threshold" and fire a phantom trigger. Check ok() to detect it.
  bool checkTrigger(uint16_t threshold, uint16_t hysteresis = 50) {
    uint16_t val = readProximity();
    if (val == READ_ERROR) return false;   // bad read -> hold state, no trigger
    uint16_t rearm = (hysteresis < threshold) ? (threshold - hysteresis) : 0;

    if (!_covered && val > threshold) {
      _covered = true;          // rising edge -> trigger!
      return true;
    }
    if (_covered && val < rearm) {
      _covered = false;         // falling edge -> re-arm
    }
    return false;
  }

private:
  // ---- VCNL4040 registers (each is 16 bits, little-endian) ----------
  static constexpr uint8_t VCNL4040_ADDR  = 0x60;
  static constexpr uint8_t REG_ALS_CONF   = 0x00;
  static constexpr uint8_t REG_PS_CONF12  = 0x03;  // PS_CONF1 (LSB) + PS_CONF2 (MSB)
  static constexpr uint8_t REG_PS_CONF3MS = 0x04;  // PS_CONF3 (LSB) + PS_MS   (MSB)
  static constexpr uint8_t REG_PS_DATA    = 0x08;
  static constexpr uint8_t REG_ID         = 0x0C;  // reads 0x0186

  uint8_t  _sda, _scl;
  uint16_t _last    = 0;
  bool     _covered = false;

  // ---- open-drain pin control ---------------------------------------
  void sdaRelease() { pinMode(_sda, INPUT);  }  // float -> pulled to 3.3V
  void sdaDrive()   { pinMode(_sda, OUTPUT); }  // drive LOW
  void sclRelease() { pinMode(_scl, INPUT);  }
  void sclDrive()   { pinMode(_scl, OUTPUT); }
  bool sdaRead()    { return digitalRead(_sda); }

  void tick() { delayMicroseconds(4); }  // ~50 kHz effective, plenty

  // Release SCL and wait for it to actually go high (clock stretching)
  void sclHighWait() {
    sclRelease();
    uint16_t timeout = 1000;
    while (!digitalRead(_scl) && --timeout) delayMicroseconds(1);
  }

  // ---- I2C primitives -----------------------------------------------
  void i2cStart() {           // SDA falls while SCL high
    sdaRelease(); sclHighWait(); tick();
    sdaDrive();   tick();
    sclDrive();   tick();
  }

  void i2cStop() {            // SDA rises while SCL high
    sdaDrive();   tick();
    sclHighWait(); tick();
    sdaRelease(); tick();
  }

  bool writeByte(uint8_t b) { // returns true if slave ACKed
    for (uint8_t i = 0; i < 8; i++) {
      (b & 0x80) ? sdaRelease() : sdaDrive();
      b <<= 1;
      tick();
      sclHighWait(); tick();
      sclDrive();
    }
    // ACK bit: release SDA, clock it, sample
    sdaRelease(); tick();
    sclHighWait();
    bool ack = !sdaRead();    // slave pulls low = ACK
    tick();
    sclDrive(); tick();
    return ack;
  }

  uint8_t readByte(bool ack) {
    uint8_t b = 0;
    sdaRelease();
    for (uint8_t i = 0; i < 8; i++) {
      tick();
      sclHighWait(); tick();
      b = (b << 1) | (sdaRead() ? 1 : 0);
      sclDrive();
    }
    // send ACK (pull low) or NACK (leave high)
    ack ? sdaDrive() : sdaRelease();
    tick();
    sclHighWait(); tick();
    sclDrive();
    sdaRelease(); tick();
    return b;
  }

  // ---- VCNL4040 register access -------------------------------------
  // Read:  S addr+W cmd  Sr addr+R  LSB(ack) MSB(nack)  P
  uint16_t read16(uint8_t cmd) {
    i2cStart();
    if (!writeByte(VCNL4040_ADDR << 1))       { i2cStop(); return 0xFFFF; }
    if (!writeByte(cmd))                      { i2cStop(); return 0xFFFF; }
    i2cStart();  // repeated start
    if (!writeByte((VCNL4040_ADDR << 1) | 1)) { i2cStop(); return 0xFFFF; }
    uint8_t lsb = readByte(true);
    uint8_t msb = readByte(false);
    i2cStop();
    return ((uint16_t)msb << 8) | lsb;
  }

  // Write: S addr+W cmd LSB MSB P
  bool write16(uint8_t cmd, uint16_t val) {
    i2cStart();
    bool ok = writeByte(VCNL4040_ADDR << 1)
           && writeByte(cmd)
           && writeByte(val & 0xFF)
           && writeByte(val >> 8);
    i2cStop();
    return ok;
  }
};

#endif  // SOFT_VCNL4040_H