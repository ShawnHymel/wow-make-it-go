# WOW Make It Go

Pinewood derby style track timer for the "Make It Go" exhibit at the [WOW Children's Museum](https://wowchildrensmuseum.org/).

## Hardware

Each track uses its own full setup (i.e. separate Arduino, sensors, etc.). The track timers do not talk to each other. Each track requires:

 * [Arduino UNO R3](https://store-usa.arduino.cc/products/arduino-uno-rev3) or [SparkFun RedBoard](https://www.sparkfun.com/sparkfun-redboard-programmed-with-arduino.html)
 * [SparkFun Qwiic Shield](https://www.sparkfun.com/catalogsearch/result/?q=proto+shield)
 * 2x [SparkFun Proximity Sensor Breakout](https://www.sparkfun.com/sparkfun-proximity-sensor-breakout-20cm-vcnl4040-qwiic.html)
 * [SparkFun 7-Segment Serial Display - Red](https://www.sparkfun.com/sparkfun-7-segment-serial-display-red.html)
 * 2x [SparkFun QwiicBus EndPoint](https://www.sparkfun.com/sparkfun-qwiicbus-endpoint.html)
 * 2x [RJ45 Female Connector Breakout](https://www.amazon.com/Cermant-RJ45-Single-Connector-Interface/dp/B0D7ZZQ8CJ/)
 * [CAT6 Patch Cable 15 ft](https://www.amazon.com/Amazon-Basics-Ethernet-High-Speed-Snagless/dp/B089MGH8W3/)
 * [CAT6 Patch Cable 6 ft](https://www.amazon.com/Cable-Matters-Ethernet-Patch-Black/dp/B0CP9XGZKM/)
 * USB Cable 15 ft ([Type-B](https://www.amazon.com/Monoprice-15-Feet-24AWG-Plated-105440/dp/B003L11CRU/) for UNO R3, [Mini-B](https://www.amazon.com/Monoprice-15-Feet-Mini-B-Ferrite-105450/dp/B002KL8N6A/) for RedBoard)
 * [USB power adapter](https://www.amazon.com/Charger-Adapter-Charging-Station-Samsung/dp/B08139YF93/)

## Connections

TODO: fill this out. For now, refer to the connections in the Arduino sketch

## Programming

Load the [track_timer](software\track_timer\track_timer.ino) sketch in Arduino. Upload it to the Arduino board.

## License

All software in this repository, unless otherwise noted, is licensed under the [Zero-Clause BSD](https://opensource.org/license/0bsd) license.

```
Permission to use, copy, modify, and/or distribute this software for
any purpose with or without fee is hereby granted.

THE SOFTWARE IS PROVIDED “AS IS” AND THE AUTHOR DISCLAIMS ALL
WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE
FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY
DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN
AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
```
