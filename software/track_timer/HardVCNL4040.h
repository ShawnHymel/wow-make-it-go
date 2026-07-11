#ifndef HARD_VCNL4040_H
#define HARD_VCNL4040_H

// HardVCNL4040 -- hardware-I2C (Wire) driver for the VCNL4040 proximity
// sensor. Header-only companion to SoftVCNL4040.h: same public API
// (begin/readProximity/last/ok/covered/checkTrigger), but talks over the
// ATmega's dedicated TWI peripheral instead of bit-banging arbitrary pins.
//
// Target: Arduino Uno / SparkFun RedBoard (5V AVR)
//
// WIRING (Qwiic cable colors) -- hardware I2C pins are FIXED on the Uno:
//   RED    -> 3.3V   (NOT 5V -- the VCNL4040 is a 3.3V part)
//   BLACK  -> GND
//   BLUE   -> SDA = A4   (the labeled SDA pin)
//   YELLOW -> SCL = A5   (the labeled SCL pin)
//
// The SparkFun breakout has 2.2k pull-ups to 3.3V on board, so no external
// resistors are needed. IMPORTANT: the AVR Wire library turns the chip's
// INTERNAL pull-ups on by default, which pull the bus toward 5V and would
// over-volt this 3.3V part. begin() disables them right after Wire.begin()
// (digitalWrite(SDA/SCL, LOW)), leaving only the 3.3V board pull-ups --
// the same open-drain-to-3.3V discipline SoftVCNL4040 enforces by hand.
//
// Because hardware I2C is a single shared bus, you can only have ONE
// VCNL4040 here (they all answer to 0x60). Put the second sensor on a
// bit-banged SDA line via SoftVCNL4040. See the accompanying .ino.

#include <Arduino.h>
#include <Wire.h>

class HardVCNL4040 {
public:
  HardVCNL4040() {}

  bool begin() {
    Wire.begin();
    // Kill the AVR's internal pull-ups (Wire.begin() enables them). They pull
    // to 5V; we want only the breakout's 3.3V pull-ups on the bus.
    digitalWrite(SDA, LOW);
    digitalWrite(SCL, LOW);
    Wire.setClock(100000);   // 100 kHz standard mode

    if (read16(REG_ID) != 0x0186) return false;  // sensor not found

    // Same config as SoftVCNL4040 so readings are directly comparable:
    // PS_CONF1/2: duty 1/40, integration time 8T, sensor ON, 16-bit output
    write16(REG_PS_CONF12, 0x080E);
    // PS_CONF3/MS: defaults + LED current 200 mA
    write16(REG_PS_CONF3MS, 0x0700);

    return true;
  }

  // Raw 16-bit proximity reading. Also caches the value; see last().
  uint16_t readProximity() {
    _last = read16(REG_PS_DATA);
    return _last;
  }

  // Sentinel returned on an I2C failure (NAK / no sensor). Proximity data
  // never legitimately reaches 0xFFFF with these settings.
  static constexpr uint16_t READ_ERROR = 0xFFFF;

  uint16_t last() const { return _last; }
  bool ok() const { return _last != READ_ERROR; }
  bool covered() const { return _covered; }

  // Force the debounce state back to "not covered" so the very next reading
  // above threshold counts as a fresh rising edge -- even if the sensor is
  // currently reading high. Used to clear a stale latch (e.g. re-arming the
  // finish sensor at the start of a new race) that would otherwise swallow
  // the next real crossing.
  void rearm() { _covered = false; }

  // Rising-edge trigger with hysteresis -- identical semantics to
  // SoftVCNL4040::checkTrigger(). A failed read never triggers and holds
  // the covered state.
  bool checkTrigger(uint16_t threshold, uint16_t hysteresis = 50) {
    uint16_t val = readProximity();
    if (val == READ_ERROR) return false;
    uint16_t rearm = (hysteresis < threshold) ? (threshold - hysteresis) : 0;

    if (!_covered && val > threshold) {
      _covered = true;
      return true;
    }
    if (_covered && val < rearm) {
      _covered = false;
    }
    return false;
  }

private:
  // ---- VCNL4040 registers (16-bit, little-endian) -- match SoftVCNL4040 ----
  static constexpr uint8_t VCNL4040_ADDR  = 0x60;
  static constexpr uint8_t REG_PS_CONF12  = 0x03;
  static constexpr uint8_t REG_PS_CONF3MS = 0x04;
  static constexpr uint8_t REG_PS_DATA    = 0x08;
  static constexpr uint8_t REG_ID         = 0x0C;  // reads 0x0186

  uint16_t _last    = 0;
  bool     _covered = false;

  // ---- register access over Wire ------------------------------------
  // Read: S addr+W cmd  Sr addr+R  LSB MSB  P
  uint16_t read16(uint8_t cmd) {
    Wire.beginTransmission(VCNL4040_ADDR);
    Wire.write(cmd);
    // endTransmission(false) sends a repeated START, not a STOP.
    if (Wire.endTransmission(false) != 0) return READ_ERROR;
    if (Wire.requestFrom((uint8_t)VCNL4040_ADDR, (uint8_t)2) != 2) return READ_ERROR;
    uint8_t lsb = Wire.read();
    uint8_t msb = Wire.read();
    return ((uint16_t)msb << 8) | lsb;
  }

  // Write: S addr+W cmd LSB MSB P
  bool write16(uint8_t cmd, uint16_t val) {
    Wire.beginTransmission(VCNL4040_ADDR);
    Wire.write(cmd);
    Wire.write(val & 0xFF);
    Wire.write(val >> 8);
    return Wire.endTransmission() == 0;
  }
};

#endif  // HARD_VCNL4040_H
