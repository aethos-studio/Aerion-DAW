#!/usr/bin/env bash
#
# Cloud Agent start script for Aerion DAW.
#
# Aerion DAW is a native JUCE GUI application, so it needs an X display to run.
# This launches a headless virtual framebuffer (Xvfb) on display :99 so the app
# can be started and screenshotted/recorded without a physical monitor.
#
# Idempotent: it will not start a second Xvfb if one is already running on :99.
# Run the app afterwards with:
#     DISPLAY=:99 "build/AerionDaw_artefacts/Debug/Aerion DAW"

set -euo pipefail

DISPLAY_NUM=":99"

if pgrep -f "Xvfb ${DISPLAY_NUM}" >/dev/null 2>&1; then
  echo "Xvfb already running on ${DISPLAY_NUM}"
else
  echo "Starting Xvfb on ${DISPLAY_NUM}"
  Xvfb "${DISPLAY_NUM}" -screen 0 1680x1050x24 -ac +extension GLX +render -noreset \
    >/tmp/xvfb.log 2>&1 &
fi

# Wait for the display to become available.
for _ in $(seq 1 20); do
  if DISPLAY="${DISPLAY_NUM}" xset q >/dev/null 2>&1; then
    echo "Xvfb ready on ${DISPLAY_NUM}"
    exit 0
  fi
  sleep 0.5
done

echo "Warning: Xvfb did not become ready on ${DISPLAY_NUM} in time" >&2
exit 0
