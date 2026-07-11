#!/bin/bash
# Firmware compile gate for the burning_dome / IO-Orb controller. Verifies
# burning_dome.ino builds clean against the ESP8266 Arduino core, the way you'd
# run tests before a release. Exit 0 = compiles; non-zero = broken build, do
# NOT flash. This is TODOS bench gate 1.
#
# Mirrors the Wanderful firmware gate (~/projects/wanderful/firmware/ci.sh):
# same tool (local arduino-cli), same staging-dir trick. arduino-cli requires
# the sketch's basename to equal its parent dir name (<dir>/<dir>.ino), which
# is already true here (burning_dome/burning_dome.ino), so staging is only to
# keep the build tree out of the repo.
#
# Board: bare ESP-12S. Default FQBN is nodemcuv2 (ESP-12E/S, 4M flash) which
# matches the module's flash and gives OTA room; override with $BD_FQBN.
# Requires config.h (copy from config.h.example) since the sketch #includes it.
set -u
cd "$(dirname "$0")"

INO=burning_dome.ino
FQBN="${BD_FQBN:-esp8266:esp8266:nodemcuv2}"
BUILD="${BD_BUILD_DIR:-/tmp/burning_dome_build}"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "FAIL firmware: arduino-cli not on PATH (brew install arduino-cli)"
  exit 1
fi
if [ ! -f "$INO" ]; then
  echo "FAIL firmware: $INO not found (run from repo root)"
  exit 1
fi
if [ ! -f config.h ]; then
  echo "FAIL firmware: config.h missing (cp config.h.example config.h, then fill it in)"
  exit 1
fi

STAGE="$BUILD/burning_dome"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$INO" config.h "$STAGE/"

echo "Compiling $INO for $FQBN ..."
if arduino-cli compile --fqbn "$FQBN" --warnings all "$STAGE"; then
  echo "----"
  echo "PASS firmware: $INO compiles for $FQBN"
  exit 0
else
  echo "----"
  echo "FAIL firmware: $INO does NOT compile for $FQBN - do not flash"
  exit 1
fi
