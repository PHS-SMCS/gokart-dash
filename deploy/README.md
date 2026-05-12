# GoKart Dash — Pi Kiosk Deployment

Boots the dashboard fullscreen on a Raspberry Pi running **Pi OS Lite** (no
desktop environment). Stack: `cage` (Wayland kiosk compositor) + `chromium`
(Ozone/Wayland) pointed at a localhost static server.

## Boot flow

```
power on
  -> systemd reaches multi-user.target
       -> gokart-dash-web.service          (python http.server on 127.0.0.1:5173)
       -> gokart-bridge.service            (teensy_bridge.py on 127.0.0.1:5174 -> /dev/serial0)
       -> getty@tty1 with autologin=gokart (drop-in: autologin.conf)
            -> ~/.bash_profile guard fires on /dev/tty1
                 -> deploy/kiosk-start.sh
                      -> cage -d -s -- chromium --kiosk --ozone-platform=wayland …
```

The browser fetches the static SPA from `:5173` and POSTs hardware commands
to the Teensy bridge on `:5174`. The bridge serializes commands per-line over
`/dev/serial0` (UART, 115200) to the Teensy `kart_controller` firmware.

SSH sessions land on `/dev/pts/N` and skip the kiosk launch entirely, so you
can log in and work normally.

## Files installed

| Path | Source in repo |
|---|---|
| `/etc/systemd/system/gokart-dash-web.service` | `deploy/gokart-dash-web.service` |
| `/etc/systemd/system/gokart-bridge.service` | `deploy/gokart-bridge.service` |
| `/etc/systemd/system/getty@tty1.service.d/autologin.conf` | `deploy/getty-tty1-autologin.conf` |
| `/etc/udev/rules.d/99-gokart-ignore-hdmi-cec.rules` | `deploy/99-gokart-ignore-hdmi-cec.rules` |
| `~/.bash_profile` | `deploy/bash_profile.snippet` (cp directly) |
| `deploy/kiosk-start.sh` | (run in place — no install) |

## From-zero install on a fresh Pi OS Lite (Bookworm/Trixie)

Assumes the user `gokart` exists and the repo is checked out at
`~/gokart-dash`. Run from an SSH session.

```bash
# 1. System packages
sudo apt update
sudo apt install -y --no-install-recommends \
    cage seatd chromium nodejs npm python3-serial libinput-tools

# 2. Seat access (logout/login — or reboot — to take effect)
sudo systemctl enable --now seatd
sudo usermod -aG seat,dialout gokart

# 3. Build the dashboard
cd ~/gokart-dash
npm install
npm run build

# 4. Drop in systemd units, udev rule, and the kiosk-launch profile
sudo install -m 644 deploy/gokart-dash-web.service     /etc/systemd/system/
sudo install -m 644 deploy/gokart-bridge.service       /etc/systemd/system/
sudo install -m 644 deploy/99-gokart-ignore-hdmi-cec.rules /etc/udev/rules.d/
sudo mkdir -p /etc/systemd/system/getty@tty1.service.d
sudo install -m 644 deploy/getty-tty1-autologin.conf \
    /etc/systemd/system/getty@tty1.service.d/autologin.conf
cp deploy/bash_profile.snippet ~/.bash_profile

# 5. Enable everything
sudo systemctl daemon-reload
sudo udevadm control --reload-rules
sudo systemctl enable --now gokart-dash-web gokart-bridge

# 6. Reboot to pick up autologin + udev
sudo reboot
```

After reboot the dashboard should fill the DSI panel with no cursor visible.

## Rebuilding the dashboard

```bash
cd ~/gokart-dash
npm run build           # writes dist/
sudo systemctl restart gokart-dash-web   # not strictly needed; SimpleHTTP re-reads dist/
```

The kiosk will pick up changes on the next Chromium reload (Ctrl+R via SSH:
`sudo chvt 1` to switch, or just reboot).

## Operating notes

- **VT switching is enabled** (`cage -s`). Ctrl+Alt+F2 from a keyboard plugged
  into the Pi drops to a console; Ctrl+Alt+F1 returns to the kiosk.
- **Chromium profile** lives at `~/.cache/chromium-kiosk/`. Delete it to reset
  state (cookies, localStorage, cached service workers).
- **No update prompts**: Chromium is configured with
  `--check-for-update-interval=31536000` and `--no-first-run`.
- **Touch input** comes through libinput automatically; no extra config
  unless you need rotation/calibration (then look at `dmesg | grep -i input`
  and Wayland output transforms).
- **No mouse cursor**: `vc4-hdmi-{0,1}` falsely advertise pointer capability
  via HDMI-CEC, which makes Cage render a cursor (see
  [cage-kiosk/cage#299](https://github.com/cage-kiosk/cage/issues/299)). The
  udev rule sets `LIBINPUT_IGNORE_DEVICE=1` on those nodes so libinput skips
  them. Verify with `sudo libinput list-devices` — only the FT5x06 touch
  device should be listed.

## Troubleshooting

```bash
# Web server status
systemctl status gokart-dash-web
journalctl -u gokart-dash-web -n 50 --no-pager

# Teensy bridge status (controls LEDs etc.)
systemctl status gokart-bridge
journalctl -u gokart-bridge -n 50 --no-pager
curl -s http://127.0.0.1:5174/api/health
curl -s http://127.0.0.1:5174/api/status
curl -s -X POST -H 'Content-Type: application/json' \
  -d '{"r":0,"g":255,"b":0}' http://127.0.0.1:5174/api/led

# What happened on tty1 (cage + chromium output goes to the tty's journal entry
# under the autologin session)
journalctl _UID=1000 -n 200 --no-pager

# Manually run the kiosk for one-off testing (must be on a real tty; will fail
# from SSH because there's no seat):
#   1. SSH in, switch console: sudo systemctl stop getty@tty1
#   2. From the physical keyboard on tty1: ./deploy/kiosk-start.sh
```

## Disabling the kiosk (back to plain console)

```bash
sudo rm /etc/systemd/system/getty@tty1.service.d/autologin.conf
sudo rmdir /etc/systemd/system/getty@tty1.service.d
sudo systemctl daemon-reload
sudo systemctl restart getty@tty1
```

The web server can be left running or disabled with
`sudo systemctl disable --now gokart-dash-web`.
