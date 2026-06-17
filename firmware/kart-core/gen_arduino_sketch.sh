#!/usr/bin/env bash
# Assemble an Arduino-IDE-flashable sketch from the canonical kart-core sources.
#
# kart-core is a PlatformIO project (src/ + lib/kartcore + ../common). The
# Arduino IDE only compiles files inside the sketch folder, so this script
# FLATTENS the real sources into arduino/kart_core/ and vendors the one
# external lib (WDT_T4). The PlatformIO tree stays the single source of truth —
# never hand-edit the generated sketch; edit the originals and re-run this.
#
# Usage:  ./gen_arduino_sketch.sh
# Then open arduino/kart_core/kart_core.ino in the Arduino IDE (Teensy 4.1).
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$HERE/arduino/kart_core"
COMMON="$HERE/../common/kart_common"
WDT="$HERE/.pio/libdeps/teensy41/WDT_T4"

rm -rf "$OUT"
mkdir -p "$OUT"

# Flatten the safety/control modules and shared wire formats.
cp "$HERE"/lib/kartcore/*.h "$HERE"/lib/kartcore/*.cpp "$OUT"/
cp "$HERE"/src/config.h "$OUT"/
cp "$COMMON"/crc16.h "$COMMON"/kart_can.h "$OUT"/

# Vendor WDT_T4 (header + template impl) so no Library-Manager step is needed.
# Rewrite its internal angle-bracket self-includes to quotes so they resolve
# inside the sketch folder.
wdt_sed='s|#include <Watchdog_t4.h>|#include "Watchdog_t4.h"|; s|#include <Watchdog_t4.tpp>|#include "Watchdog_t4.tpp"|'
if [[ -d "$WDT" ]]; then
  sed "$wdt_sed" "$WDT"/Watchdog_t4.h   > "$OUT"/Watchdog_t4.h
  sed "$wdt_sed" "$WDT"/Watchdog_t4.tpp > "$OUT"/Watchdog_t4.tpp
else
  echo "WARN: WDT_T4 not found at $WDT — run 'pio pkg install' first, or the"
  echo "      sketch will fail to find Watchdog_t4.h." >&2
fi

# The main translation unit becomes the .ino. Only rewrite the one angle-bracket
# include that pointed at the vendored lib; bundled libs (<USBHost_t36.h>,
# <Wire.h>, <Arduino.h>) stay as-is.
sed 's|#include <Watchdog_t4.h>|#include "Watchdog_t4.h"|' \
  "$HERE"/src/main.cpp > "$OUT"/kart_core.ino

cat > "$OUT"/README.md <<'EOF'
# kart_core — Arduino IDE sketch (GENERATED)

Do **not** edit files here. This folder is assembled from the canonical
PlatformIO sources by `firmware/kart-core/gen_arduino_sketch.sh`. Edit the
originals (`src/`, `lib/kartcore/`, `firmware/common/`) and re-run that script.

Open `kart_core.ino` in the Arduino IDE, select **Teensy 4.1**, and upload.
USBHost_t36, FlexCAN_T4, and Wire ship with Teensyduino; WDT_T4 is vendored here.
EOF

echo "Generated $OUT"
ls "$OUT"
