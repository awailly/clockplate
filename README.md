# clockplate

A Fibonacci clock for the [Soldered Inkplate 2](https://soldered.com/product/inkplate-2/)
(2.13", 212×104, black/white/red e-paper, ESP32). Five squares valued
1, 1, 2, 3, 5 — nothing on the plate explains how to read them.

Sister project of [stockplate](https://github.com/awailly/stockplate), same
structure.

## Reading it

- **Hours** = sum of the red squares (12-hour dial).
- **Minutes** = sum of the black squares, × 5.
- A split red/black square counts in both sums; white squares count for
  nothing.

Example: 3 and 1 red, the other 1 split, 2 and 5 black → hours
3+1+1 = 5, minutes (2+5+1)×5 = 40 → 5:40. Precision is 5 minutes, and
most times have several valid colorings — the firmware picks one at
random each refresh, so the same time rarely looks the same twice.

## How it works

- The 3-color panel takes ~24 s per refresh, so the firmware renders the
  *upcoming* 5-minute step and starts the refresh early: the panel
  finishes flashing right when that step starts.
- Battery friendly: one refresh per 5 minutes (the face's precision),
  deep sleep in between, WiFi off except for an hourly NTP re-sync done
  after the render (the ESP32 sleep clock drifts a few seconds per hour;
  each sync snaps the alignment back), CPU at 80 MHz. The ~24 s of panel
  work per refresh still dominates consumption.

## Build and flash

1. Install [PlatformIO](https://platformio.org/).
2. Copy `src/config_example.h` to `src/config.h` and fill in your WiFi
   credentials and timezone.
3. Plug the Inkplate 2 and run:

   ```sh
   pio run -t upload
   ```

## License

MIT
