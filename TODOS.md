# burning_dome (IO-Orb) TODOS

> Renders as a live kanban via agent-kanban: sections below are columns,
> struck items collect into a Done lane. Registered with
> `board.py register --todos TODOS.md --name burning_dome`, view at the hub.
> This file stays the single source of truth; the board is a read-only view.

The forward queue, and the ONLY place open/planned work lives. This is the
road-ahead; BUILD_LOG.md is the rear-view. They must not bleed into each other:
shipped work lives in git + BUILD_LOG (never here), and forward work lives here
(never as a "next steps" tail on a BUILD_LOG entry). When an item ships, do NOT
delete it: strike it through (`~~...~~`) and append `(done YYYY-MM-DD)`.

Sections ordered by when they block. Every gate has a PASS CRITERION and a
FAILURE INTERPRETATION - a bad result should be a lookup, not a debate.

## 1. Verification / bench gates (before field trust)

GATE: all green before flashing/OTA-ing a build to the live orb.

1. **Compiles clean, no legacy collisions** (Arduino compiles every
   `.ino` in the folder; `969fd03`). Pass: `arduino-cli compile`
   succeeds with only `burning_dome.ino` in the root (variants stay in
   `archive/`). Fail: a duplicate-symbol error means a stray `.ino`
   re-entered the sketch dir -> move it back to `archive/`.
2. **Off is actually dark** (`55797f7`). Pass: manual power-off AND a
   scheduled stop both drive every pixel to 0 (verify by eye + a
   `strip.getPixelColor` readback if instrumented). Fail: a dim glow
   means an off path skipped the explicit dark frame -> route it
   through the same `allColor(0)`+`show()`.
3. **WiFi survives the animation loop.** Pass: web UI stays responsive
   and OTA completes while animations run at the fastest speed (30 ms).
   Fail: dropped requests/OTA timeouts mean a frame is blocking the
   loop -> confirm one-frame-per-flag, no `delay()` in the render path.
4. **Schedule ignores pre-sync time.** Pass: after a cold boot the orb
   does NOT act on the schedule until NTP has synced (year >= 2025).
   Fail: an off/on flip at boot means `checkSchedule()` trusted a ~1970
   clock -> add the sane-year guard before the compare.
5. **Heap holds over soak.** Pass: `getMaxFreeBlockSize()` stable (not
   just `getFreeHeap()`) over a >=48 h run with periodic web hits.
   Fail: a downward drift is String fragmentation in the handlers ->
   move HTML to flash/`F()` or serve from LittleFS.
6. **Weather-alarm end-to-end on hardware** (WATG-1/2/3; code shipped
   2026-07-10, compiles + parse host-tested, NOT yet flashed). Set an
   alarm 2 min out via the app and confirm: (a) at the minute it starts
   a gentle ~3 s-period throb for 30 min then restores prior state
   (dark if it was off); (b) the throb color matches the day's weather
   - yellow sunny / blue cloudy / grey rain-fog; (c) alarm survives a
   reboot and does NOT fire pre-NTP-sync; (d) with WiFi down the fetch
   falls back to neutral grey and does not hang the loop; (e)
   `getMaxFreeBlockSize()` stable across repeated daily fetches.
   Fail on (b): re-check `weatherCodeToColor` cut points / fiber hue
   calibration. Fail on (d): the blocking HTTP GET is starving the loop
   -> shorten timeout / guard. This gate stays OPEN until flashed.

## 1.5 Fab / hardware gates (per board or enclosure change)

- Bench-verify GPIO2 boots clean with the pixel driver attached (must
  be HIGH at reset). Coupon: power-cycle 10x, expect 10 clean boots.
- Confirm 3.3 V -> WS2812 logic-high margin on the real supply; add a
  level shifter only if the first pixel is flaky.
- Confirm flash layout leaves OTA staging room before relying on OTA.

## 2. Calibrations (in situ)

- Animation `speedVal` range (30-500 ms) tuned by eye on the actual
  fiber bundle - fiber diffusion changes the perceived speed vs a bare
  strip.
- Default schedule (18:00-23:00 Pacific) confirmed against the room's
  actual use.

## 3. Planned work

### Feature: weather-alarm throb glow

At a user-set alarm time, the orb does a gentle throb whose COLOR
encodes today's weather: **yellow = sunny, blue = cloudy**. Three
cards, dependency-ordered; each ships behind its own acceptance gate.
Open product/architecture decisions are pinned in section 4 - resolve
those before the card they block, not after.

- ~~**[WATG-1] Alarm time: app control + persistence.**~~ (done
  2026-07-10 - code complete, compiles clean; on-hardware verify in
  bench gate 6.) Add a THIRD
  scheduled event ("alarm", HH:MM) alongside the existing on/off
  schedule. New web-UI control + `GET /alarm` / `GET /setalarm`
  endpoints; persist to LittleFS via the SAME store as the schedule
  (extend `loadSchedule`/`saveSchedule`, don't fork it). Alarm fire is
  edge-triggered off the once-per-second `checkSchedule()` tick, and
  MUST honor the NTP sane-year guard (bench gate 4) so it can't fire on
  a ~1970 clock at boot. Also fire-once-per-day: guard against
  re-firing every second within the alarm minute.
  Verify: set an alarm 2 min out, confirm it fires exactly once at the
  minute boundary, survives a reboot, and does NOT fire pre-NTP-sync.
  Depends on: nothing. Blocks: WATG-3.

- ~~**[WATG-2] Weather fetch + condition -> color.**~~ (done
  2026-07-10 - code complete; parse + color mapping host-unit-tested
  incl. a control; on-hardware verify in bench gate 6.) ~30 min BEFORE the
  alarm (see WATG-3), fetch current conditions from **Open-Meteo over
  plain HTTP** (keyless, no TLS - verified 2026-07-10 that
  `http://api.open-meteo.com/v1/forecast?latitude=..&longitude=..&current_weather=true`
  returns 200 with no HTTPS redirect) and reduce the returned WMO
  `weathercode` to a color:
    - `0` (clear) and `1` (mainly clear) -> **yellow** (sunny)
    - `2`,`3` (partly cloudy, overcast) -> **blue** (cloudy)
    - `45`,`48` (fog) and `51-67`,`80-82` (drizzle/rain) -> **grey**
    - snow / thunderstorm (`71-77`,`85-86`,`95-99`) -> grey for now
      (residual WATG-D2 tail: give them their own color later if wanted)
  Lat/lon come from `config.h` (add `WEATHER_LAT`/`WEATHER_LON` to
  `config.h.example`). Cache the resulting color + a fetched-at
  timestamp; the alarm reads the cache, never blocks on a live fetch.
  The HTTP GET is brief but blocking - run it off the once-per-second
  tick, not the render path, and degrade gracefully: on fetch failure
  or stale cache fall back to a defined neutral color, never hang.
  Verify: point at a mocked sunny (code 0) and cloudy (code 3) response
  and confirm the cached color is yellow vs blue, and a rain code (61)
  gives grey; kill WiFi and confirm the neutral fallback, no hang,
  `getMaxFreeBlockSize()` stable across repeated fetches.
  Depends on: nothing (WATG-D1 resolved). Blocks: WATG-3.

- ~~**[WATG-3] Throb-glow animation + alarm trigger.**~~ (done
  2026-07-10 - code complete, compiles clean; on-hardware verify in
  bench gate 6.) New gentle-throb
  animation: brightness follows a slow sine (period ~2-4 s) at the
  weather color, driven by `millis()` phase so it fits the existing
  one-frame-per-`animFlag` Ticker model (no `delay()`). At alarm fire
  (WATG-1) it overrides the current mode with WATG-2's color for a set
  duration/until dismissed, then returns to prior state (and, if that
  state is "off", pushes the explicit dark frame per the 55797f7
  lesson). Throb by scaling the COLOR, not by hammering
  `strip.setBrightness()` each frame (repeated 8-bit rescale loses
  color resolution). Window: **30 min** (WATG-D3) - throb from alarm
  time for 30 minutes, then restore prior state. Also kick WATG-2's
  fetch ~30 min before the alarm so the color is fresh when it fires.
  Verify: at alarm fire the orb throbs in the WATG-2 color for 30 min
  then restores; on a sunny vs cloudy day it is visibly yellow vs blue;
  after the window it restores the pre-alarm state exactly (dark if it
  was off).
  Depends on: WATG-1, WATG-2.

- Persist brightness/mode/color/power across reboot (LittleFS already
  mounted for the schedule; extend the same store).
- Move the large `handleRoot` HTML into `F()`/PROGMEM or a LittleFS
  file to cut heap pressure (couples to bench gate 5).
- Optional: captive-portal / runtime WiFi config so `config.h` isn't
  required at flash time.

## 4. Held / gated decisions

- Level shifter on the pixel data line - HELD until bench gate shows
  the first pixel is actually marginal at 3.3 V. Design it in, let the
  measurement decide whether it ships.

- ~~**[WATG-D1] Weather transport + source**~~ RESOLVED 2026-07-10:
  **Open-Meteo over plain HTTP, keyless.** Verified `http://api.open-meteo.com`
  returns 200 with no HTTPS redirect, so NO API key and NO BearSSL/TLS -
  the ~16-22 KB heap landmine is avoided entirely. Location = hardcoded
  lat/lon in `config.h` (`WEATHER_LAT`/`WEATHER_LON`). Baked into WATG-2.

- ~~**[WATG-D2] Condition -> color mapping**~~ RESOLVED 2026-07-10:
  yellow=sunny (WMO 0-1), blue=cloudy (2-3), **grey=rain/fog** (45/48,
  51-67, 80-82). Snow/thunderstorm folded into grey for now (residual:
  give them a distinct color later if wanted). Exact yellow/blue/grey
  RGB still to be calibrated on the fiber bundle at build time. Baked
  into WATG-2.

- ~~**[WATG-D3] Throb duration + dismissal**~~ RESOLVED 2026-07-10:
  fixed **30-minute** window from the alarm time, then restore prior
  state. (No web "dismiss" for v1 - can add later.) Baked into WATG-3.
