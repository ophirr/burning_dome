#!/bin/bash
# PreToolUse(Bash) guard for burning_dome. Blocks RAW firmware-flash commands
# so every flash must go through ./flash.sh (serial-first; OTA preflights
# /flashinfo). Installed after the 2026-07-10 OTA brick (an unverified WiFi
# SSID case error stranded the OTA-only device). See HARDWARE.md / BUILD_LOG.md.
#
# Reads the tool-call JSON on stdin. Prints a PreToolUse "deny" decision ONLY
# for raw espota / arduino-cli upload / esptool write|erase-flash commands that
# do not go through flash.sh; prints nothing (allow) otherwise. Always exits 0.
cmd=$(jq -r '.tool_input.command // empty')
if printf '%s' "$cmd" | grep -qE 'espota|arduino-cli[[:space:]]+upload|write[_-]flash|erase[_-]flash' \
   && ! printf '%s' "$cmd" | grep -q 'flash\.sh'; then
  printf '%s' '{"hookSpecificOutput":{"hookEventName":"PreToolUse","permissionDecision":"deny","permissionDecisionReason":"burning_dome: raw firmware flashing is blocked - use ./flash.sh (serial-first; OTA preflights /flashinfo). See HARDWARE.md / BUILD_LOG.md 2026-07-10."}}'
fi
