# Hardware - burning_dome (IO-Orb)

Serial-verified target facts. **Any flash must match this.** flash.sh pins the
FQBN here; change it in ONE place, re-verify over serial, update this file.

## Board / MCU (verified 2026-07-10 via `esptool flash-id` + photo)

| Field | Value | How known |
|---|---|---|
| Module | **Adafruit HUZZAH ESP8266** breakout (ESP-12S), on protoboard | photo 2026-07-10 |
| MCU | ESP8266EX, 160 MHz, 26 MHz xtal | esptool |
| Flash size | **4 MB** (4194304 bytes) | esptool flash-id (mfr `1c`, dev `3016`) |
| Flash mode | DIO (`flashMode:2` from /flashinfo) | /flashinfo |
| MAC | `60:01:94:4a:b3:7d` | esptool + ARP |
| Canonical FQBN | `esp8266:esp8266:nodemcuv2` (4 MB, FS default) | matches real 4 MB; /flashinfo `match:true` |
| Core / SDK | esp8266 3.1.2 / SDK 2.2.2-dev | /flashinfo |

## Pins

- **GPIO2** - NeoPixel data (24 px) AND onboard blue LED AND boot-strap (must be HIGH at boot).
- Data line drives a fiber-optic bundle.

## Flashing this board

**No auto-reset** - a plain 4-wire FTDI (3.3 V) with no DTR-to-GPIO0 / RTS-to-RESET
wiring. Two onboard buttons: **GPIO0 (FLASH)** and **RESET**.

- **Bootloader entry:** hold GPIO0, tap RESET, release GPIO0. (Silent; LEDs dark.)
- **Run after flash:** tap RESET with GPIO0 released. (esptool's "hard reset via
  RTS" is a no-op here since RTS isn't wired.)
- **Serial console:** `/dev/cu.usbserial-*` @ 115200. The boot log prints WiFi
  `Status:` (3 = connected) + IP, the fastest truth about a flash.
- Recovery reflash (single manual bootloader entry):
  `esptool --port <p> --chip esp8266 --before no-reset --after hard-reset write-flash 0x0 <bin>`

## Network

- WiFi: **`TizoNet`** (case-sensitive: `Tizonet` is NOT `TizoNet`, see BUILD_LOG
  2026-07-10). 2.4 GHz. DHCP at `192.168.5.185`, hostname `io-orb`.
- Creds live in `config.h` (gitignored): `WIFI_SSID`, `WIFI_PASS`,
  `WEATHER_LAT`, `WEATHER_LON`.
