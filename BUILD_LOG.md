# burning_dome (IO-Orb) Build Log

Engineering learnings, newest first. Each entry is a thing learned the
hard way, with the commit(s) that carry the fix. Git messages hold the
full detail; this log is the pattern library so the same lesson doesn't
get bought twice. WRITE ENTRIES THE SAME DAY - both predecessor
projects (Wanderful, KEXP box) paid for expensive backfills.

Entry schema:

    ### <what happened, as a claim> - `<commit>` (vX.Y.Z if versioned)
    2-6 lines: what happened, what was actually wrong, the fix with
    the real numbers. Name who caught it if a human did.
    **Lesson: the transferable rule, one or two sentences, bold.**

Every correction of a confident-but-wrong output is an entry candidate
(and a cognitive-surrender field log candidate). Rollbacks get the
measurement data inline, where attempt N+1 will read it.

<!-- BUILD-LOG-HARVEST: insert entries below, newest-first -->

---

## 2026-07-10 - weather-alarm throb glow (WATG-1/2/3)

### Open-Meteo's JSON carries `weathercode` TWICE; the naive `indexOf` reads the wrong one - (uncommitted)
The response has `"current_weather_units":{...,"weathercode":"wmo code"}`
BEFORE `"current_weather":{...,"weathercode":0}`. A bare
`payload.indexOf("\"weathercode\":")` lands on the UNITS block, and
`.substring(+14).toInt()` on `"wmo code"` returns 0 - which happens to
be a valid code (clear/sunny), so it fails SILENTLY as a permanent
"always sunny" bug. Fix: anchor on `"current_weather":` first, then
search for `weathercode` from that offset. Caught before flashing by
fetching the real payload (probing the primary artifact) and by a
host-side parse test with a control that must fail (broken payload ->
-999, not 0).
**Lesson: when scraping a JSON field by string search, a duplicated key elsewhere in the document is a silent-wrong-answer trap, doubly so when the wrong value is itself in-range. Anchor the search on the containing object, and prove it with a control payload that must fail to parse.**

### Keyless + plain-HTTP weather sidesteps the ESP8266 TLS heap tax - (uncommitted)
WATG-D1 was framed as "which weather API, and how much BearSSL heap
will TLS cost." Probing the actual endpoint dissolved the question:
`http://api.open-meteo.com/v1/forecast?...&current_weather=true`
returns 200 over plain HTTP with no HTTPS redirect and needs no API
key. So NO BearSSL (~16-22 KB/handshake avoided), no key management.
Compile confirms the feature added only ~12.6 KB flash (33%->34%) and
~2% RAM, zero IRAM (no interrupt code added).
**Lesson: before budgeting for the expensive version of a dependency (TLS, auth), probe whether the cheap version actually works - the vendor's plain-HTTP endpoint erased a whole heap-budget subproject.**

### Throb overrides on/off; the alarm and the schedule contend for the same LEDs - (uncommitted)
The 30-min throb forces the orb on and intercepts the render, but the
1 Hz schedule tick could fight it (turn off mid-throb). Resolved by
priority: while `alarmActive`, `checkSchedule()` is skipped and the
throb saves/restores the prior `orbOn`; on end, if the restored state
is "off" it pushes an explicit `allColor(0)` dark frame (the `55797f7`
lesson - "stop updating" is not "off"). Window expiry is evaluated at
the TOP of the 1 Hz tick so the restored state is authoritative that
same second.
**Lesson: two features driving one actuator need an explicit priority + save/restore contract, not two independent writers; and every path that ends a lit state must route through the one dark-frame primitive.**

---

## 2026-07-10 - joined an existing repo (backfilled from git history)

Repo cloned from github.com/ophirr/burning_dome and adopted as a
managed build. The three entries below are reconstructed from existing
commit subjects, not observed same-day - treat their numbers as
approximate until re-verified against the hardware.

### Legacy sketches broke the build; Arduino compiles every .ino in the sketch dir - `969fd03`
The Arduino toolchain concatenates/compiles ALL `.ino` files in the
sketch folder, so the older `2pot_2button` and `no_block` variants
collided (duplicate `setup()`/`loop()`, redefinitions) with the live
sketch. Fix: moved the legacy variants to `archive/` so they are out
of the compilation set.
**Lesson: an Arduino sketch folder is a single translation unit across every .ino it contains; a spare variant left beside the main sketch is not inert, it is a redefinition. Park alternates outside the build dir.**

### Dim glow persisted after the orb was powered off (scheduled or manual) - `55797f7`
Turning the orb "off" stopped issuing new animation frames but left
the last frame latched in the strip, so pixels held a dim glow instead
of going dark. The off path set state but never pushed an all-black
frame to the strip. Fix: on power-off, write `allColor(0)` / clear +
`strip.show()` once so the LEDs actually go dark.
**Lesson: a NeoPixel strip holds its last frame indefinitely; "stop updating" is not "off." An off state must push one explicit dark frame, and every power path (manual + scheduled) must route through it.**

### Non-blocking rearchitecture: WiFi gets the loop, LEDs run off a Ticker flag - `1b942dc` (initial commit)
Early pot-controlled variants (now in `archive/`) blocked in the
animation with `delay()`, which starved `server.handleClient()` /
`ArduinoOTA.handle()` and made the web UI and OTA unreliable. The
shipped design inverts control: `loop()` services WiFi every
iteration, a `Ticker` fires at `speedVal` ms to set `animFlag`, and
exactly ONE animation frame renders per flag.
**Lesson: on a single-core ESP8266 the network stack and the pixel loop compete for the same thread; any `delay()` in the animation is stolen from WiFi. Make WiFi the base loop and gate LED work behind a timer flag, one frame per tick.**

---

## Standing reference (platform physics live here as they are learned)

See PLATFORM_PHYSICS.md for the full interrogation. Governing
constants for this build, measured/researched provenance noted:

- **NeoPixel `show()` disables interrupts** for ~30us/pixel (~720us
  for 24 px). Tolerable here, but it is why a longer strip would start
  to corrupt WiFi timing - the coupling is real, just small at N=24.
- **GPIO2 is a boot strapping pin** on the ESP-12S AND the NeoPixel
  data line here. It must be HIGH at boot; a pixel driver pulling it
  around during reset is a known brownout/boot-loop risk to watch.
- **ESP8266 heap is non-compacting**: `ESP.getFreeHeap()` sums free
  bytes and lies about what you can allocate; `ESP.getMaxFreeBlockSize()`
  (largest contiguous block) is the metric that predicts alloc
  failure. Same lesson as KEXP's CircuitPython GC.
- **NTP time is invalid until sync**: `configTime()` returns
  immediately but `time()` reads ~1970 until the first sync lands.
  Any schedule comparison must guard on a sane year before trusting
  the clock (see TODOS bench gate).
- Predecessor pattern libraries: home/wanderful/BUILD_LOG.md (TPU,
  OpenSCAD, laser fixtures, ESP32 power), src/kexp-er/BUILD_LOG.md
  (CircuitPython heap, WiFiNINA networking, Sonos, Glowforge dialect).
