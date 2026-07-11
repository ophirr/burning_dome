# Platform Physics - burning_dome (IO-Orb)

Week-one homework, not blanks: every substrate in the build gets its
3-5 GOVERNING CONSTANTS researched or measured before design leans on
intuition imported from other materials/platforms. Substrates in play:
**ESP8266 (ESP-12S)** compute, **WS2812/NeoPixel** LED radio-timing,
**WiFi/HTTP/OTA/mDNS/NTP** services, **LittleFS** flash storage.

Unanswered rows are research questions WITH DEADLINES. Answered rows
graduate into BUILD_LOG's Standing reference with provenance
(measured > vendor doc > community > training data).

## Compute platform - ESP8266 (ESP-12S)

- **Memory model.** Heap is non-compacting. `ESP.getFreeHeap()` is a
  sum and lies about allocatability; `ESP.getMaxFreeBlockSize()` is
  the largest contiguous block = the number that predicts failure.
  The web handlers build `String`s (handleRoot serves a large HTML
  page) - String concatenation fragments the heap. WATCH: free heap
  and max-block over a multi-day soak.
- **Single thread, cooperative.** No preemption. Every `delay()` or
  blocking call in the animation is stolen directly from
  `server.handleClient()` / `ArduinoOTA.handle()`. This is WHY the
  Ticker-flag architecture exists (see BUILD_LOG initial-commit entry).
- **GPIO2 strapping.** GPIO2 must be HIGH at boot to enter flash-run
  mode, and it is ALSO the NeoPixel data pin here. Any driver stage or
  level shifter that pulls it low during reset can wedge the boot.
- **ISR context.** `onAnimTimer()` runs in the Ticker ISR and is
  correctly `IRAM_ATTR` + touches only a `volatile bool`. Rule holds:
  ISR sets a flag, `loop()` does the work.
- **OTA flash budget.** ArduinoOTA needs roughly free-flash >= sketch
  size to stage the new image. Confirm the chosen flash layout leaves
  room (sketch is ~24 KB source; compiled + libs is the real number).

## LED timing - WS2812 / NeoPixel

- `strip.show()` disables interrupts for ~30 us/pixel (~720 us at
  N=24) to bit-bang the 800 kHz protocol. Small here; the coupling to
  WiFi timing grows linearly with pixel count.
- The strip **latches its last frame** with no refresh - "stop
  sending" is not "off". Off requires one explicit dark frame (the
  `55797f7` lesson).
- Level: ESP8266 is 3.3 V; WS2812 wants ~0.7*Vdd logic high. At 5 V
  Vdd the first pixel may be marginal on a bare 3.3 V line - note
  whether a level shifter or 3.3 V-ish supply is in the actual build.

## External services / protocols

- **NTP is async.** `configTime()` returns before any sync; `time()`
  reads ~1970 until the first packet lands (seconds to tens of
  seconds after WiFi up). Schedule logic MUST guard on a plausible
  year before acting. Also: DST rule string `PST8PDT,M3.2.0,M11.1.0`
  is hardcoded US-Pacific.
- **mDNS/OTA/HTTP** all share the one loop; none may block.

## Fab / enclosure

- Physical orb + fiber-optic bundle + protoboard-mounted ESP-12S.
  Enclosure and fiber-coupling geometry not yet in this repo -
  document dimensions in source-context/ when the build reaches it.

## Answers

| Substrate | Constant | Value | Provenance | Date |
|---|---|---|---|---|
| ESP8266 | Alloc-predicting metric | `getMaxFreeBlockSize()`, not `getFreeHeap()` | community/SDK docs | 2026-07-10 |
| NeoPixel x24 | `show()` interrupt-off window | ~720 us (~30 us/px) | datasheet arithmetic | 2026-07-10 |
| NeoPixel | Off requires explicit dark frame | latches last frame | measured (commit 55797f7) | 2026-07-10 |
| NTP | Time valid only after sync | reads ~1970 until sync | SDK behavior | 2026-07-10 |
| GPIO2 | Boot-strap + pixel data shared | must be HIGH at boot | ESP-12S datasheet | 2026-07-10 |
