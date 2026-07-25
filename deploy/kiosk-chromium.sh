#!/usr/bin/env bash
# Cage's single client. Runs INSIDE the Wayland session (cage sets
# WAYLAND_DISPLAY/XDG_RUNTIME_DIR for us), applies the panel rotation to the
# output — a wlroots output transform rotates touch input along with the
# display, so no separate touch calibration is needed — then execs Chromium.
#
# Kept separate from kiosk-start.sh because the transform must be applied from
# within the compositor session, not before cage starts. Override via env:
#   KIOSK_OUTPUT    (default DSI-1)   the wlr-randr output name
#   KIOSK_TRANSFORM (default 180)     normal|90|180|270|flipped|flipped-90|...
set -u

OUTPUT="${KIOSK_OUTPUT:-DSI-1}"
TRANSFORM="${KIOSK_TRANSFORM:-180}"

# The output can take a moment to appear after cage comes up; retry briefly.
for _ in $(seq 1 20); do
    if wlr-randr --output "$OUTPUT" --transform "$TRANSFORM" 2>/dev/null; then
        break
    fi
    sleep 0.25
done

exec /usr/bin/chromium "$@"
