# Track Timer — design notes

Non-blocking gravity-track timer: an object rests on a start gate (sensor 1),
is released, rolls, and trips a finish line (sensor 2). Two VCNL4040 proximity
sensors, one SparkFun serial 4-digit 7-segment display.

## Display format

`SSS.C` — seconds with one tenth (decimal point after digit 3, `DP3`).
The ones-of-seconds digit is always drawn, so zero reads `0.0`, not `.0`.
Range spans to 999.9 s but a run is capped at the timeout ceiling below.

## State machine

Everything is driven by `millis()` and the sensors' own edge/level state —
no `delay()`, no blocking.

| State     | Display                          | Exit condition                                   | Next    |
|-----------|----------------------------------|--------------------------------------------------|---------|
| `READY`   | `0.0` at boot, else last result  | object placed on gate (sensor 1 rising edge)     | `PRIMED`|
| `PRIMED`  | flashing `GO` (400 ms)           | object leaves gate (sensor 1 falling edge)       | `RUNNING`|
| `RUNNING` | live time                        | finish line (sensor 2) **or** 99.9 s timeout     | `READY` |
| `ERROR`   | `Err1` / `Err2`                  | both sensors read OK again                       | `READY` |

- `READY` doubles as the post-race state: the final time stays frozen on the
  display until a new object re-primes the gate (that is the "reset").
- Sensor 2 is only polled while `RUNNING`, so a finish-before-start can't
  register — there is no false-start case to handle.
- Timeout (99.9 s) just freezes at the ceiling and returns to `READY`; it is
  not treated as an error.

## Driver support (`SoftVCNL4040.h`)

Three small additions so the sketch can see gate edges and read failures:

- `checkTrigger()` ignores a failed read (`READ_ERROR` = `0xFFFF`) instead of
  treating it as a huge value and firing a phantom trigger.
- `covered()` — debounced presence level, used to detect the *release*
  (falling) edge of the start gate.
- `ok()` — `_last != READ_ERROR`, drives the `ERROR` state.

## Display driver (`Serial7Seg.h`)

Unchanged. The sketch only pushes over SPI when the shown content changes
(`showTenths()` caches the last numeric value; `showText()` handles GO/errors/
blank and toggles the decimal point off).
