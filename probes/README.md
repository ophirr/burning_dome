# Probe Suite - burning_dome (IO-Orb)

Bench diagnostics, run before every flash/OTA like a test suite before
a release. This is an embedded build, so probes are on-target
measurements + controls, not CAD booleans. Each maps to a bench gate in
`../TODOS.md`. Rules that make a probe a verification instead of noise:

1. Every probe is PAIRED WITH A CONTROL that must fail (a deliberately
   broken input / a known-bad state). A pass with no failing control
   proves nothing about sensitivity.
2. Probe the metric that PREDICTS failure, not the convenient proxy:
   **largest contiguous block (`ESP.getMaxFreeBlockSize()`), not
   `ESP.getFreeHeap()`** - the KEXP heap lesson, same shape here.
3. Physical/behavioral findings from a flash -> the probe that would
   have caught it (bug -> test). Any "will X hold / does Y go dark"
   question -> answered by writing the probe, which then stays.

## Probes (each -> a TODOS bench gate)

- **heap_probe** (gate 5): print `getFreeHeap()` AND
  `getMaxFreeBlockSize()` at boot and every N minutes over a soak.
  Control: hammer `/` (the big HTML handler) in a loop and confirm the
  max-block number moves - if it never moves, the probe isn't
  observing the fragmentation path.  See `heap_probe.h`.
- **off_is_dark** (gate 2): after power-off, read back pixel color with
  `strip.getPixelColor(i)` for all i; expect all 0. Control: sample the
  same readback while ON and confirm it is non-zero (proves the
  readback actually reflects strip state).
- **wifi_under_load** (gate 3): run animations at 30 ms while cur/ab
  hits `/status` in a loop and an OTA push runs; expect no dropped
  requests / OTA timeout. Control: temporarily add a `delay(200)` in
  the render path and confirm the probe FAILS - proves it is sensitive
  to loop starvation.
- **schedule_presync** (gate 4): cold-boot with schedule enabled and
  log whether `checkSchedule()` acts before NTP sync. Expect no action
  until year >= 2025. Control: force `time()` to a 1970 value and
  confirm the guard blocks the compare.
- **boot_stability** (fab gate): power-cycle 10x with the pixel driver
  attached on GPIO2; expect 10 clean boots (no boot-loop from the
  strapping-pin conflict).

## References (copy and adapt, don't re-derive)

- Embedded heap/liveness diagnostics: KEXP `diag.py` pattern
  (binary-search for largest block, cheap counters at steady state,
  probe at boot + in error handlers).
- CAD boolean probes + volume-classifying runner (for the enclosure,
  when it lands): `home/wanderful/hardware/probes.scad`,
  `home/wanderful/tools/run_probes.sh`.
