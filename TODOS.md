# burning_dome (IO-Orb) TODOS

<!-- ===================================================================
     AGENT SDLC BOARD (agent-sdlc / agent-kanban). The six lifecycle
     columns below are the board. Cards are prefixed with a hardware-path
     LABEL: [FW]/[WEB] are agent-loopable (the /sdlc loop implements + PRs
     them); [BENCH]/[FAB]/[CAL] are human bench work through the SAME
     columns + gates. Gate 1 (Backlog->Todo) and Done are HUMAN-ONLY; the
     agent is structurally blocked from writing Done (done-gate.sh).
     NOTE: the board is STRIKE-DRIVEN - a struck card (strikethrough, a
     done-marker, or [x]) renders in the Done lane no matter which section
     it sits in, so move a card to Done by DRAGGING it on the hub (board.py
     writes the strike + relocation cleanly) rather than hand-editing a
     multi-line card. Deep reference detail goes in a card's own body lines
     or BUILD_LOG, NOT a separate section (## / ### headers each become a
     board column). THIS BOARD LIVES ON main - never edit it on a work branch.
     Registered: board.py register --todos TODOS.md --name burning_dome
     ==================================================================== -->

## Backlog

Loop-workable + human cards, ordered by when they block. A human drags one to
Todo (Gate 1) to start it.

- [ ] [FAB] NeoPixel noise hardening - the fan-glitch cure (the real fix; NG-1
  firmware only masks it). 330-470 ohm series resistor on GPIO2 data + 470-1000
  uF (>=10 V) electrolytic across the strip 5V/GND at pixel 1 (+ leg to 5V);
  optional 3.3->5 V level shifter if pixel 1 stays flaky; put the orb on a
  different outlet than the fan. AC: switching a fan on no longer glows the off
  strip.
- [ ] [FW] Persist power/mode/brightness/color across reboot (LittleFS, extend
  the schedule store - don't fork it). AC: after a reboot the orb restores its
  last on/off + mode/brightness/color, not the compiled defaults.
- [ ] [FW] Move handleRoot HTML to PROGMEM/F() or serve from LittleFS, to cut
  heap. AC: getMaxFreeBlockSize() stable over a 48 h soak w/ periodic web hits.
- [ ] [FW] Runtime WiFi config / captive portal so config.h creds aren't
  compile-time. AC: set SSID/pass from the phone with no reflash.
- [ ] [BENCH] Weather-alarm on-hardware soak. AC: alarm survives reboot + does
  NOT fire pre-NTP-sync; WiFi-down fetch falls back to grey without hanging the
  loop; maxFreeBlock stable across daily fetches. (throb + color already
  confirmed on hardware 2026-07-10.)
- [ ] [BENCH] GPIO2 clean-boot coupon w/ pixel driver attached. AC: 10/10 clean
  boots (GPIO2 must be HIGH at reset).
- [ ] [BENCH] WiFi survives the animation loop. AC: web UI responsive + OTA
  completes while animations run at the fastest speed (30 ms).
- [ ] [BENCH] Schedule ignores pre-sync time. AC: a cold boot does not act on
  the schedule until NTP has synced (year >= 2025).

## Todo

- (empty - a human promotes a Backlog card here; Gate 1)

## In Progress

- (none)

## Held

- (none - the level-shifter call folded into the NeoPixel-hardening [FAB] card:
  add a 3.3->5 V shifter only if pixel 1 stays flaky after the resistor + cap.)

## Ready for Review

- ~~[ ] [FW/WEB] Alarm controls: configurable length (1-120 min) + throb up/down~~ (done 2026-07-10)
  times + turn-off-mid-throb reverts to the scheduled state. (PR #4 merged +
  OTA-deployed.)
- ~~[ ] [FW/WEB] Alarm UI clarity: rename to "Alarm duration", derived "one throb~~ (done 2026-07-10)
  ~= Xs" readout, link toggle mirroring throb up/down, and Cache-Control:
  no-store so OTA UI changes aren't masked by a stale page. (PR #5; compiles
  clean; OTA after merge.)

- ~~[ ] [WEB] Move the "one throb ~= Xs" readout inline, left of the link toggle. (PR #6; cosmetic; compiles clean.)~~ (done 2026-07-10)

- [ ] [FW/WEB] Flip weather colors (grey=cloudy, blue=rain/fog) + simplify throb control to "one throb ~= Xs" + a bare mirror checkbox. (PR #7; compiles clean.)

## Done

_HUMAN-ONLY gate. A card lands here via a MERGED PR ([FW]/[WEB]) or a bench
sign-off ([BENCH]/[FAB]/[CAL]) - moved by Ophir, never the agent (done-gate.sh).
Bulk shipped history lives in git + BUILD_LOG.md._

- ~~[FW] NG-1 off-state glitch guard - VCC-dip event detector + nightly 21:00-23:00 PST safety re-dark~~ (done 2026-07-10; PR #2; OTA-deployed, glitchEvents/vcc live in /status)
- ~~[WEB] Mode selector -> dropdown (the button row is now a select)~~ (done 2026-07-10; PR #3; OTA-deployed)
- ~~[FW] Weather-alarm throb glow (WATG-1/2/3) + runtime throb-speed + flash gate~~ (done 2026-07-10; PR #1; throb + weather color confirmed on the orb)
