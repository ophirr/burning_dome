# burning_dome (IO-Orb)

NeoPixel fiber-optic orb controller. ESP-12S (ESP8266) on protoboard
driving a 24-pixel WS2812/NeoPixel strip into a fiber bundle, with a
WiFi web interface, OTA updates, mDNS, and an NTP-scheduled on/off.

## What it does

- 5 animation modes: rainbow-cycle, solid color, rainbow, theater
  chase, theater-chase-rainbow.
- Web UI (port 80) for mode, brightness, color (hue), speed, power.
- Daily on/off schedule (default 18:00-23:00 US-Pacific), persisted to
  LittleFS, driven by NTP time.
- Non-blocking architecture: `loop()` services WiFi/OTA/mDNS every
  iteration; a `Ticker` sets `animFlag` at the current speed and
  exactly one animation frame renders per flag. See BUILD_LOG for why.

## Layout

- `burning_dome.ino` - the live firmware.
- `config.h.example` - copy to `config.h` (gitignored) and fill in
  `WIFI_SSID` / `WIFI_PASS`. OTA password reuses `WIFI_PASS`.
- `archive/` - superseded pot/button variants; kept OUT of the sketch
  dir because Arduino compiles every `.ino` in the folder together.
- `BUILD_LOG.md` - engineering lessons, newest first.
- `PLATFORM_PHYSICS.md` - governing constants of the ESP8266 / NeoPixel
  / NTP substrates.
- `TODOS.md` - forward queue (bench gates -> fab gates -> planned),
  also a live kanban board.
- `probes/` - bench diagnostics run before flash/OTA.
- `source-context/` - raw inputs, datasheets, wiring notes, captures.

## Build / flash

Arduino toolchain, ESP8266 core. Ensure only `burning_dome.ino` is in
the root sketch folder (variants stay in `archive/`), create `config.h`
from the example, then compile & upload (USB first flash, OTA after).

Board hostname: `io-orb` (mDNS `io-orb.local`).
