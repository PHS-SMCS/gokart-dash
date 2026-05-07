#!/usr/bin/env bash
# Capture --dry-run outputs from every hardware-scripts CLI subcommand.
#
# Usage:
#   ./scripts/refactor_baseline.sh before    # capture pre-refactor outputs
#   ./scripts/refactor_baseline.sh after     # capture post-refactor outputs
# Then `diff -ur before/ after/` should be empty. Cwd-independent — derives
# the repo root from the script's own location.
set -euo pipefail

command -v python3 >/dev/null 2>&1 || {
  echo "ERROR: python3 not on PATH; cannot capture baselines" >&2
  exit 1
}

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

LABEL="${1:-before}"
OUT_ROOT="$REPO_ROOT/docs/superpowers/specs/python-refactor-baselines/${LABEL}"
mkdir -p "$OUT_ROOT"

run() {
  local name="$1"; shift
  local outfile="$OUT_ROOT/${name}.txt"
  local out code
  # Combined stdout+stderr; record exit code on its own line.
  set +e
  out="$("$@" 2>&1)"
  code=$?
  set -e
  printf '%s\n--- exit: %d\n' "$out" "$code" > "$outfile"
}

cd "$REPO_ROOT/hardware-scripts/host"

# kartctl: every subcommand with --dry-run (representative args)
run kartctl-ping        python3 kartctl.py --dry-run ping
run kartctl-status      python3 kartctl.py --dry-run status
run kartctl-help-cmd    python3 kartctl.py --dry-run help-cmd
run kartctl-safe        python3 kartctl.py --dry-run safe
run kartctl-disarm      python3 kartctl.py --dry-run disarm
run kartctl-hall        python3 kartctl.py --dry-run hall
run kartctl-output-on   python3 kartctl.py --dry-run output --name brake --state on
run kartctl-output-off  python3 kartctl.py --dry-run output --name brake --state off
run kartctl-speed-low   python3 kartctl.py --dry-run speed --mode low
run kartctl-speed-med   python3 kartctl.py --dry-run speed --mode medium
run kartctl-speed-high  python3 kartctl.py --dry-run speed --mode high
run kartctl-reverse-on  python3 kartctl.py --dry-run reverse --state on
run kartctl-reverse-off python3 kartctl.py --dry-run reverse --state off
run kartctl-brake-on    python3 kartctl.py --dry-run brake --state on
run kartctl-brake-off   python3 kartctl.py --dry-run brake --state off
run kartctl-contactor-on  python3 kartctl.py --dry-run contactor --state on
run kartctl-contactor-off python3 kartctl.py --dry-run contactor --state off
run kartctl-throttle-zero python3 kartctl.py --dry-run throttle --percent 0
run kartctl-throttle-five python3 kartctl.py --dry-run throttle --percent 5
run kartctl-led         python3 kartctl.py --dry-run led --r 100 --g 200 --b 50
run kartctl-esc-write   python3 kartctl.py --dry-run esc-write --hex A55A0102
run kartctl-esc-read    python3 kartctl.py --dry-run esc-read --max 32
run kartctl-can-tx      python3 kartctl.py --dry-run can-tx --id 0x123 --data DEADBEEF
run kartctl-can-poll    python3 kartctl.py --dry-run can-poll --max 4
run kartctl-validate    python3 kartctl.py --dry-run validate bringup --profile bench

# esc_tool subcommands
run esc-read   python3 esc_tool.py --dry-run read --max 32
run esc-write  python3 esc_tool.py --dry-run write --hex A55A0102
run esc-watch  python3 esc_tool.py --dry-run watch --max 32 --interval 0.2 --duration 1.0

# can_tool subcommands
run can-tx     python3 can_tool.py --dry-run tx --id 0x123 --data DEADBEEF
run can-poll   python3 can_tool.py --dry-run poll --max 4

echo "captured ${LABEL} baselines under ${OUT_ROOT}"
