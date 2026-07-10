# Track Timer — design notes

Non-blocking gravity-track timer: an object rests on a start gate (sensor 1),
is released, rolls, and trips a finish line (sensor 2). Two VCNL4040 proximity
sensors, one SparkFun serial 4-digit 7-segment display.

## Display format

`SSS.C` — seconds with one tenth (decimal point after digit 3, `DP3`).
The ones-of-seconds digit is always drawn, so zero reads `0.0`, not `.0`.
Range spans to 999.9 s, which is also the timeout ceiling below.

## Sensing

Neither sensor has a fixed trigger level. `setup()` averages
`AMBIENT_SAMPLES` (10) readings from each sensor with the track clear, stores
them as `ambient1` / `ambient2`, and thereafter treats
`ambient + TRIGGER_DELTA` (500) as "covered". This is why the track must be
empty at boot.

`checkTrigger()` applies 50 counts of hysteresis: a sensor that has fired
re-arms only once its reading falls back below `threshold - 50`, so a hovering
hand cannot machine-gun triggers.

## State machine

Everything is driven by `millis()` and the sensors' own edge/level state —
no `delay()`, no blocking.

| State      | Display                          | Exit condition                                   | Next       |
|------------|----------------------------------|--------------------------------------------------|------------|
| `READY`    | `0.0` at boot, else last result  | object placed on gate (sensor 1 rising edge)     | `PRIMED`   |
| `PRIMED`   | steady `-GO-`                    | object leaves gate (sensor 1 falling edge)       | `RUNNING`  |
| `RUNNING`  | live time                        | finish line (sensor 2 rising edge)               | `FINISHED` |
| `RUNNING`  | live time                        | 999.9 s timeout                                  | `READY`    |
| `FINISHED` | final time, flashing then held   | `FINISH_FLASH_COUNT` flashes elapsed             | `READY`    |
| `ERROR`    | `Err1` / `Err2`                  | both sensors read OK again                       | `READY`    |

- `READY` doubles as the post-race state: the final time stays frozen on the
  display until a new object re-primes the gate (that is the "reset").
- `FINISHED` blinks the result `FINISH_FLASH_COUNT` (4) times, toggling every
  `FINISH_FLASH_MS` (300 ms) and starting from blank — about 2.4 s — then holds
  the time and drops to `READY`.
- A sensor 1 rising edge re-primes from **any** state except `ERROR`, not just
  from `READY`. Re-staging an object on the gate mid-race abandons the run in
  progress rather than waiting for the timeout.
- Timeout freezes the display at the 999.9 s ceiling and returns to `READY`;
  it is not treated as an error and does not flash.

### Both sensors are polled every pass

`loop()` calls `checkTrigger()` on both sensors unconditionally — that call is
what refreshes each sensor's `ok()` and `covered()` state, so it cannot be
skipped. What is state-dependent is whether the returned edge is *consumed*:
sensor 2's rising edge is only acted on in `RUNNING`.

An edge raised outside `RUNNING` is therefore discarded, and — because of the
hysteresis above — that sensor stays latched `covered` until it re-arms. An
object left sitting on the finish line before the race starts will not produce
a finish trigger when the race runs; it has to be lifted clear first.

### Errors

A failed read on either sensor (`READ_ERROR` = `0xFFFF`) puts the machine in
`ERROR` immediately, from any state, and `loop()` returns early. Recovery — one
pass where both sensors read cleanly — resets to `READY` and clears the display
to `0.0`, so an error mid-race loses that run's time. `prevCovered1` is
resynced on the way out so the recovery does not fire a phantom release edge.

`ERROR_REBOOT_LIMIT` (10) *consecutive* failed passes on either sensor calls
`hardReboot()`. The per-sensor counters reset on any fully clean pass. Note
that a pass costs single-digit milliseconds, so ten of them go by fast: in
practice `Err1` / `Err2` flickers rather than sits on the display before the
reboot. Raise the limit if you want the code readable by a human on the bench.

`hardReboot()` arms the watchdog at its shortest timeout (15 ms) and spins,
which resets registers and peripherals too — unlike a jump-to-zero soft reset.

## Watchdog

`wdt_enable(WDTO_1S)` at the end of `setup()`, `wdt_reset()` at the top of
`loop()`. Three things about the placement are load-bearing:

- **Armed last, not first.** `setup()` blocks for >2 s (two settle delays plus
  `AMBIENT_SAMPLES * 50 ms` of calibration). Arming before that resets us
  mid-calibration, forever.
- **Fed at the top of `loop()`, not the bottom.** The `ERROR` branch `return`s
  early. A feed at the bottom would be skipped on every failed read, letting
  the watchdog pre-empt the `ERROR_REBOOT_LIMIT` path that is supposed to own
  that reboot (and its `Err1` / `Err2` display).
- **`MCUSR` / `WDRF` cleared first.** After a watchdog reset the WDT is still
  armed; `WDRF` must be cleared before `WDE` can be. Optiboot does this before
  handing over — ours is belt-and-braces, and would not save a stock bootloader
  (that one gets reset inside its own startup delay, before `setup()` runs).

It exists to catch one thing the `ERROR` state cannot. The AVR `Wire` library
spins with **no timeout** in `endTransmission()` / `requestFrom()`, so a
finish-line VCNL4040 that wedges SDA low hangs `loop()` forever, with no failed
read to count. This is accepted as-is rather than fixed in the driver: it is
unlikely, and a bounded TWI implementation is a lot of code to own. The
watchdog is the cheap backstop.

`SoftVCNL4040::sclHighWait()` already bounds its own spin at 1000 µs, so the
bit-banged start gate degrades into `READ_ERROR` and the `ERROR` state instead
of hanging — it never needed the watchdog.

`hardReboot()` is unaffected: `wdt_enable()` performs the WDCE/WDE timed
sequence, so re-arming a running 1 s watchdog down to 15 ms is legal.

## Bus layout

Both VCNL4040s answer to I2C address `0x60`, so they can't share a bus.
Each sensor gets its own:

- **Sensor 1 (start gate)** — bit-banged software I2C (`SoftVCNL4040`) on
  SDA pin 2 / SCL pin 5.
- **Sensor 2 (finish line)** — hardware I2C (`HardVCNL4040`, the `Wire`
  library) on the ATmega's fixed TWI pins A4 (SDA) / A5 (SCL).

Both drivers expose the same API (`begin` / `checkTrigger` / `ok` / `covered` /
`last`), so the state machine treats the two sensors identically.

The VCNL4040 is a 3.3 V part and both breakouts carry 2.2 kΩ pull-ups to 3.3 V,
so neither driver may enable the AVR's internal pull-ups. `SoftVCNL4040`
emulates open-drain by hand (drive low, or release to float).
`HardVCNL4040::begin()` has to actively switch them back off, because
`Wire.begin()` turns them on.

## Driver support (`SoftVCNL4040.h` / `HardVCNL4040.h`)

Three small additions so the sketch can see gate edges and read failures:

- `checkTrigger()` ignores a failed read (`READ_ERROR` = `0xFFFF`) instead of
  treating it as a huge value and firing a phantom trigger.
- `covered()` — debounced presence level, used to detect the *release*
  (falling) edge of the start gate.
- `ok()` — `_last != READ_ERROR`, drives the `ERROR` state.

## Display driver (`Serial7Seg.h`)

Unchanged. The sketch only pushes over SPI when the shown content changes:
`showTenths()` caches the last numeric value, `showText()` handles
`-GO-` / errors / blank, and `showMode` tracks which of the two is on screen so
the decimal point is toggled only on an actual number↔text transition.
