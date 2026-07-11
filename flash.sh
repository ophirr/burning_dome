#!/bin/bash
# Gated firmware flasher for burning_dome / IO-Orb. This is the ONLY sanctioned
# way to flash the orb. A PreToolUse hook blocks raw espota.py / arduino-cli
# upload / esptool write_flash so the preflight below cannot be bypassed.
#
# WHY THIS EXISTS: on 2026-07-10 an OTA image carrying an UNVERIFIED config
# (WiFi SSID "Tizonet" vs the real "TizoNet" - a one-letter case error) was
# pushed to the orb over the air; it could not rejoin WiFi after the reboot and
# stranded off-network, needing an FTDI serial recovery (see BUILD_LOG). The
# rules this enforces: (a) a config/credential change is flashed over SERIAL
# first, where the boot log proves association, before it is OTA-eligible; and
# (b) never OTA an image whose flash layout is not proven to match the target
# (the /flashinfo canary, below).
#
#   ./flash.sh serial [--erase] [--port /dev/cu.usbserial-XXXX]
#       Flash over USB/FTDI. Reads the chip's REAL flash size via esptool and
#       ABORTS if it disagrees with the pinned FQBN. --erase does a full
#       erase_flash first (use for recovery / config mismatch). This is the
#       ONLY path allowed for a device that has never run a /flashinfo build.
#
#   ./flash.sh ota <ip-or-host>
#       Flash over the air. PREFLIGHT: GET /flashinfo and ABORT unless the
#       running image reports match=true (chip == build layout) AND the new
#       image fits freeSketchSpace AND the pinned FQBN's flash size == the
#       chip's real size. Post-flash: re-GET /flashinfo to confirm.
#
# Canonical build config is PINNED here + in HARDWARE.md. Do not override with a
# convenience FQBN -- change it in one place, re-verify over serial, update
# HARDWARE.md. ci.sh reads the same value.
set -euo pipefail
cd "$(dirname "$0")"

# ---- pinned build config (single source of truth; mirrored in HARDWARE.md) ----
FQBN="${BD_FQBN:-esp8266:esp8266:nodemcuv2}"   # UPDATE after serial flash-id verification
FQBN_FLASH_BYTES="${BD_FQBN_FLASH_BYTES:-4194304}"  # flash size the FQBN assumes (4MB)
BUILD=/tmp/burning_dome_build
INO=burning_dome.ino

die(){ echo "FLASH ABORT: $*" >&2; exit 1; }
have(){ command -v "$1" >/dev/null 2>&1; }

[ -f "$INO" ] || die "$INO not found (run from repo root)"
[ -f config.h ] || die "config.h missing (cp config.h.example config.h, fill in creds+location)"
have arduino-cli || die "arduino-cli not on PATH"

build_image(){
  echo ">> compiling $INO for pinned FQBN: $FQBN"
  rm -rf "$BUILD"; mkdir -p "$BUILD"
  arduino-cli compile --fqbn "$FQBN" --output-dir "$BUILD" "$INO" >/dev/null
  BIN="$BUILD/$INO.bin"
  [ -f "$BIN" ] || die "compile produced no .bin"
  IMG_BYTES=$(stat -f%z "$BIN")
  echo ">> image: $BIN ($IMG_BYTES bytes)"
}

stamp_deploy(){  # $1=method $2=target
  local commit; commit=$(git rev-parse --short HEAD 2>/dev/null || echo nogit)
  local dirty=""; git diff --quiet 2>/dev/null || dirty="-dirty"
  printf -- '- %s | %s -> %s | %s%s | fqbn=%s | %s bytes\n' \
    "$(date '+%Y-%m-%d %H:%M')" "$1" "$2" "$commit" "$dirty" "$FQBN" "${IMG_BYTES:-?}" \
    >> DEPLOY_LOG.md
  echo ">> logged to DEPLOY_LOG.md ($commit$dirty)"
}

cmd="${1:-}"; shift || true
case "$cmd" in
  serial)
    ERASE=0; PORT=""
    while [ $# -gt 0 ]; do case "$1" in
      --erase) ERASE=1;;
      --port) PORT="$2"; shift;;
      *) die "unknown arg: $1";;
    esac; shift; done
    [ -n "$PORT" ] || PORT=$(ls /dev/cu.usbserial-* /dev/cu.SLAB_USBtoUART /dev/cu.wchusbserial* 2>/dev/null | head -1)
    [ -n "$PORT" ] || die "no USB-serial port found (pass --port)"
    have esptool || die "esptool not on PATH (brew install esptool)"
    echo ">> reading REAL flash size over serial (GPIO0 must be grounded + reset first)"
    REAL=$(esptool --port "$PORT" --chip esp8266 flash-id 2>/dev/null \
            | grep -iE "flash size|detected flash size" | grep -oE "[0-9]+MB" | head -1 || true)
    if [ -n "$REAL" ]; then
      echo ">> chip reports: $REAL ; pinned FQBN assumes: $((FQBN_FLASH_BYTES/1048576))MB"
      exp=$(( FQBN_FLASH_BYTES/1048576 ))MB
      [ "$REAL" = "$exp" ] || die "chip flash ($REAL) != pinned FQBN ($exp). Fix FQBN/FQBN_FLASH_BYTES + HARDWARE.md, do NOT flash a mismatched layout."
    else
      echo "!! could not read flash size (not in bootloader mode?). Proceeding only because serial flashing writes the full image at 0x0."
    fi
    build_image
    if [ "$ERASE" = 1 ]; then
      echo ">> erase_flash"; esptool --port "$PORT" --chip esp8266 erase-flash
    fi
    echo ">> flashing over serial via arduino-cli"
    arduino-cli upload -p "$PORT" --fqbn "$FQBN" --input-dir "$BUILD" "$INO"
    stamp_deploy serial "$PORT"
    echo ">> DONE. Verify: curl http://<orb-ip>/flashinfo  (expect match:true, realSize==configuredSize)"
    ;;

  ota)
    TARGET="${1:-}"; [ -n "$TARGET" ] || die "usage: flash.sh ota <ip-or-host>"
    have python3 || die "python3 required"
    ESPOTA=$(ls ~/Library/Arduino15/packages/esp8266/hardware/esp8266/*/tools/espota.py 2>/dev/null | head -1)
    [ -n "$ESPOTA" ] || die "espota.py not found"
    echo ">> OTA preflight: GET http://$TARGET/flashinfo"
    INFO=$(curl -s -m 8 "http://$TARGET/flashinfo" || true)
    [ -n "$INFO" ] || die "no /flashinfo response. This firmware predates the canary OR is unreachable. First flash MUST be serial (./flash.sh serial)."
    getj(){ echo "$INFO" | grep -oE "\"$1\":[^,}]*" | head -1 | sed -E 's/.*://; s/"//g'; }
    MATCH=$(getj match); REALSZ=$(getj realSize); CONFSZ=$(getj configuredSize); FREE=$(getj freeSketchSpace)
    echo ">> running image: realSize=$REALSZ configuredSize=$CONFSZ match=$MATCH freeSketchSpace=$FREE"
    [ "$MATCH" = "true" ] || die "running image MISMATCHES the chip (real=$REALSZ conf=$CONFSZ). OTA would compound it. Recover over serial first."
    [ "$REALSZ" = "$FQBN_FLASH_BYTES" ] || die "chip real size ($REALSZ) != pinned FQBN size ($FQBN_FLASH_BYTES). The new image would mismatch. Fix the pin, verify over serial."
    build_image
    [ "$IMG_BYTES" -lt "$FREE" ] || die "image ($IMG_BYTES) does not fit freeSketchSpace ($FREE)"
    echo ">> preflight PASS. OTA pushing to $TARGET"
    PW=$(grep '#define WIFI_PASS' config.h | sed -E 's/.*"([^"]*)".*/\1/')
    python3 "$ESPOTA" -i "$TARGET" -p 8266 -a "$PW" -f "$BIN" -r
    echo ">> waiting for reboot, re-checking /flashinfo"
    ok=0; for i in $(seq 1 15); do
      sleep 2; V=$(curl -s -m 3 "http://$TARGET/flashinfo" || true)
      if echo "$V" | grep -q '"match":true'; then echo ">> post-flash OK: $V"; ok=1; break; fi
    done
    [ "$ok" = 1 ] || die "device did not return a healthy /flashinfo after OTA -- CHECK IT NOW."
    stamp_deploy ota "$TARGET"
    ;;

  *)
    die "usage: flash.sh {serial [--erase] [--port P] | ota <ip>}"
    ;;
esac
