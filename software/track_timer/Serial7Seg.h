#ifndef SERIAL7SEG_H
#define SERIAL7SEG_H

// Serial7Seg -- header-only driver for the SparkFun Serial 7-Segment
// Display in SPI mode. Drop this next to your sketch and
// #include "Serial7Seg.h".
//
// Based on Jim Lindblom's SparkFun example (public domain).
//
// Circuit (hardware SPI on an Uno / RedBoard):
//   Arduino ------------- Serial 7-Segment
//     5V   -----------------  VCC
//     GND  -----------------  GND
//     ssPin -----------------  SS    (any digital pin; passed to ctor)
//     11   -----------------  SDI    (MOSI, fixed by hardware SPI)
//     13   -----------------  SCK    (fixed by hardware SPI)
//
// Only the SS pin is configurable; SDI/SCK are the board's hardware SPI
// pins. You can drive several displays from one SPI bus by giving each
// its own SS pin.

#include <Arduino.h>
#include <SPI.h>

class Serial7Seg {
public:
  // Decimal-point / colon / apostrophe bit masks for setDecimals().
  // OR them together, e.g. setDecimals(DP2 | COLON).
  //   bit map: (X)(X)(Apos)(Colon)(Digit4)(Digit3)(Digit2)(Digit1)
  static constexpr uint8_t DP1   = 0x01;
  static constexpr uint8_t DP2   = 0x02;
  static constexpr uint8_t DP3   = 0x04;
  static constexpr uint8_t DP4   = 0x08;
  static constexpr uint8_t COLON = 0x10;
  static constexpr uint8_t APOS  = 0x20;

  explicit Serial7Seg(uint8_t ssPin) : _ss(ssPin) {}

  // Bring up SPI, configure the SS pin, and clear the display.
  void begin() {
    pinMode(_ss, OUTPUT);
    digitalWrite(_ss, HIGH);
    SPI.begin();
    clear();
  }

  // Clear the display and reset the cursor to digit 1.
  void clear() { sendCmd(CMD_CLEAR); }

  // Print up to the first 4 characters of a C string. Stops early at a
  // null terminator, so shorter strings only touch the leading digits.
  void print(const char* s) {
    beginTxn();
    for (uint8_t i = 0; i < 4 && s[i] != '\0'; i++) SPI.transfer((uint8_t)s[i]);
    endTxn();
  }

  // Convenience overload for Arduino Strings.
  void print(const String& s) { print(s.c_str()); }

  // Convenience overload for integers: right-justified into 4 digits,
  // e.g. print(42) -> "  42". Values beyond 4 digits are truncated to
  // the leading 4, matching the raw display's behavior.
  void print(int value) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%4d", value);
    print(buf);
  }

  // Brightness: 0 (dimmest) .. 255 (brightest).
  void setBrightness(uint8_t value) { sendCmd(CMD_BRIGHTNESS, value); }

  // Turn decimals/colon/apostrophe on or off. A 1 bit = on. Use the
  // DP1..DP4 / COLON / APOS constants above.
  void setDecimals(uint8_t mask) { sendCmd(CMD_DECIMAL, mask); }

  // Move the cursor to position 0..3 (leftmost..rightmost digit).
  void setCursor(uint8_t pos) { sendCmd(CMD_CURSOR, pos); }

private:
  // ---- display command bytes ----------------------------------------
  static constexpr uint8_t CMD_CLEAR      = 0x76;
  static constexpr uint8_t CMD_DECIMAL    = 0x77;
  static constexpr uint8_t CMD_CURSOR     = 0x79;
  static constexpr uint8_t CMD_BRIGHTNESS = 0x7A;

  uint8_t _ss;

  // The display tops out around 250 kHz, so slow the bus down (the same
  // rate the old SPI_CLOCK_DIV64 gave on a 16 MHz board). Using a
  // transaction means this coexists safely with other SPI devices.
  SPISettings _settings{250000, MSBFIRST, SPI_MODE0};

  void beginTxn() {
    SPI.beginTransaction(_settings);
    digitalWrite(_ss, LOW);
  }
  void endTxn() {
    digitalWrite(_ss, HIGH);
    SPI.endTransaction();
  }

  // Single command byte, optionally followed by one data byte.
  void sendCmd(uint8_t cmd) {
    beginTxn();
    SPI.transfer(cmd);
    endTxn();
  }
  void sendCmd(uint8_t cmd, uint8_t data) {
    beginTxn();
    SPI.transfer(cmd);
    SPI.transfer(data);
    endTxn();
  }
};

#endif  // SERIAL7SEG_H