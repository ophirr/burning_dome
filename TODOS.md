# burning_dome (IO-Orb) TODOS

<!-- ===================================================================
     AGENT SDLC BOARD (agent-sdlc). The six lifecycle columns below are
     the operational board (also the agent-kanban view). Cards are prefixed
     with a hardware-path LABEL: [FW]/[WEB] are agent-loopable (the /sdlc
     loop implements + PRs them); [BENCH]/[FAB]/[CAL] are human bench work
     through the SAME columns + gates. The ## Detail section at the bottom is
     reference, ignored by the parser. Gate 1 (Backlog->Todo) and Done
     (merged PR for code cards / human bench sign-off for physical cards) are
     HUMAN-ONLY. THIS BOARD LIVES ON main - never edit it on a work branch.
     Registered: board.py register --todos TODOS.md --name burning_dome
     ==================================================================== -->

## Backlog

Loop-workable + human cards, ordered by when they block. A human drags one to
Todo (Gate 1) to start it.

- [ ] [FAB] NeoPixel noise hardening - the fan-glitch cure. 330-470 ohm series
  resistor on GPIO2 data + 470-1000 uF (>=10 V) electrolytic across the strip
  5V/GND at pixel 1 (+ leg to 5V); put the orb on a different outlet than the
  fan. AC: switching a fan on no longer glows the off strip. (detail: NG-1)
- [ ] [FW] Persist power/mode/brightness/color across reboot (LittleFS, extend
  the schedule store - don't fork it). AC: after a reboot the orb restores its
  last on/off + mode/brightness/color, not the compiled defaults.
- [ ] [FW] Move handleRoot HTML to PROGMEM/F() or serve from LittleFS, to cut
  heap. AC: getMaxFreeBlockSize() stable over a 48 h soak w/ periodic web hits.
- [ ] [FW] Runtime WiFi config / captive portal so config.h creds aren't
  compile-time. AC: set SSID/pass from the phone with no reflash.
- [ ] [BENCH] Weather-alarm on-hardware soak (remaining WATG-6 sub-items). AC:
  alarm survives reboot + does NOT fire pre-NTP-sync; WiFi-down fetch falls back
  to grey without hanging the loop; maxFreeBlock stable across daily fetches.
  (throb + color already confirmed on hardware 2026-07-10.) (detail: WATG-6)
- [ ] [BENCH] GPIO2 clean-boot coupon w/ pixel driver attached. AC: 10/10 clean
  boots (GPIO2 must be HIGH at reset).
- [ ] [BENCH] WiFi survives the animation loop. AC: web UI responsive + OTA
  completes while animations run at the fastest speed (30 ms).
- [ ] [BENCH] Schedule ignores pre-sync time. AC: a cold boot does not act on
  the schedule until NTP has synced (year >= 2025).

## Todo

- (empty - a human promotes a Backlog card here; Gate 1)

## In Progress

- [~] [FW] NG-1 off-state glitch guard. VCC-dip EVENT detector (ADC_MODE(ADC_VCC);
  a dip past a rolling baseline = a conducted-transient event -> bump
  glitchEvents + re-assert dark; expose glitchEvents/vcc/baseline/min in /status)
  + a nightly 21:00-23:00 PST safety re-dark for the radiated case the VCC probe
  can't see. AC: glitchEvents increments when the fan switches on; the off-strip
  glow clears; compiles clean. (branch: ng1-glitch-guard; detail: NG-1)
- [~] [WEB] Mode selector -> dropdown. Replace the row of mode buttons at the top
  with a single `<select>`. AC: choosing a mode from the dropdown sets it (fires
  `/mode?m=`); the dropdown reflects the current mode on load/sync; the button
  row is gone; other controls unchanged. (branch: mode-dropdown)

## Held

- (none - the level-shifter call folded into the NeoPixel-hardening [FAB] card:
  add a 3.3->5 V shifter only if pixel 1 stays flaky after the resistor + cap.)

## Ready for Review

- (empty)

## Done

_Cards arrive here via a MERGED PR ([FW]/[WEB]) or a human bench sign-off
([BENCH]/[FAB]/[CAL]). Bulk shipped history lives in git + BUILD_LOG.md._

- ~~[FW] Weather-alarm throb glow (WATG-1/2/3) + runtime throb-speed + flash
  gate~~ (done 2026-07-10; PR #1 merged. At a set alarm the orb throbs 30 min in
  the day's weather color, keyless Open-Meteo over HTTP; throb speed 1-8 s via a
  web slider; /flashinfo canary + gated flash.sh + block-raw-flash hook. Throb +
  weather color confirmed on the physical orb.)

## Detail

Reference for the cards above; ignored by the board parser.

### NG-1 - off-state glitch (fan transient)
Symptom: with the orb OFF, a fan switching on makes the idle WS2812s latch a dim
glow that persists (the firmware stops refreshing when off). The ESP cannot read
the strip, so we cannot detect the glitch directly; we detect its likely cause.
- Firmware (In Progress): VCC dip = a conducted-transient EVENT (via
  ESP.getVcc(), A0 free). Caveats: WiFi TX ripples VCC (baseline + debounce
  guard false fires); MISSES pure radiated EMI (no VCC trace); polling may miss a
  very brief dip. Nightly 21:00-23:00 PST re-dark is the radiated-case fallback.
- Hardware cure ([FAB], the real fix): series R on data + big electrolytic across
  strip 5V/GND at pixel 1 (+ leg to 5V, >=10 V); optional 3.3->5 V level shifter
  if pixel 1 stays flaky; orb on a different outlet than the fan.

### WATG-6 - weather-alarm on-hardware acceptance
(a) at the alarm minute a ~3 s throb runs 30 min then restores prior state (dark
if off); (b) color matches weather (yellow sunny / blue cloudy / grey rain-fog);
(c) survives reboot, no pre-NTP-sync fire; (d) WiFi-down -> neutral grey, no loop
hang; (e) maxFreeBlock stable across daily fetches. (a)+(b) confirmed 2026-07-10;
(c)/(d)/(e) pending -> the [BENCH] soak card.
