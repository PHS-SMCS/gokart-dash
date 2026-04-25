#!/usr/bin/env bash
# Launch the GoKart Dash kiosk: Cage (Wayland) + Chromium fullscreen.
# Invoked from ~/.bash_profile on tty1 autologin.

set -euo pipefail

KIOSK_URL="${KIOSK_URL:-http://127.0.0.1:5173}"
PROFILE_DIR="${XDG_CACHE_HOME:-$HOME/.cache}/chromium-kiosk"
mkdir -p "$PROFILE_DIR"

# Wait briefly for the static server to come up. systemd usually has it
# ready before tty1 autologin, but be defensive on cold boot.
for _ in $(seq 1 20); do
    if curl -fsS -o /dev/null --max-time 1 "$KIOSK_URL/"; then
        break
    fi
    sleep 0.5
done

CHROMIUM_FLAGS=(
    --kiosk "$KIOSK_URL"
    --ozone-platform=wayland
    --enable-features=UseOzonePlatform,OverlayScrollbar
    --user-data-dir="$PROFILE_DIR"
    --no-first-run
    --no-default-browser-check
    --noerrdialogs
    --disable-infobars
    --disable-translate
    --disable-features=TranslateUI,Translate
    --disable-session-crashed-bubble
    --disable-pinch
    --overscroll-history-navigation=0
    --check-for-update-interval=31536000
    --password-store=basic
    --autoplay-policy=no-user-gesture-required
    # 7" Waveshare DSI panel is 800x480 native; pin DPR so layout
    # doesn't get scaled by Chromium's HiDPI heuristics.
    --force-device-scale-factor=1
    --touch-events=enabled
)

exec cage -d -s -- /usr/bin/chromium "${CHROMIUM_FLAGS[@]}"
