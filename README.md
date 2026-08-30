# clockplate

A minimal clock for the [Soldered Inkplate 2](https://soldered.com/product/inkplate-2/)
(2.13", 212×104, black/white/red e-paper, ESP32). Shows the time in large
digits and the date in French below, refreshed once per minute.

Sister project of [stockplate](https://github.com/awailly/stockplate), same
structure.

## How it works

- The 3-color panel takes ~24 s per refresh, so the firmware renders the
  *upcoming* minute and starts the refresh early: the panel finishes
  flashing right when that minute starts.
- Battery friendly: deep sleep between refreshes, WiFi off except for an
  hourly NTP re-sync done after the render (the ESP32 sleep clock drifts a
  few seconds per hour; each sync snaps the alignment back), CPU at 80 MHz.
  The ~24 s of panel work per minute still dominates consumption — e-paper
  refreshing every minute is inherently costly, expect a day or two on the
  stock cell rather than weeks.

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
