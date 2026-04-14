# Host Serial Utilities

Operator-side tools for controlling and diagnosing the Teensy 4.1 kart firmware over USB serial.

## Files

- `kartctl.py` — primary CLI with safety-aware command wrappers
- `can_tool.py` — dedicated CAN transmit/poll helper
- `esc_tool.py` — ESC serial passthrough helper
- `serial_link.py` — shared transport code

## Requirements

```bash
python3 -m pip install pyserial
```

## `kartctl.py` Quick Start

Common flags:

- `--port` (default `/dev/ttyACM0`)
- `--baud` (default `115200`)
- `--dry-run` (do not send commands)
- `--arm-seconds` (ARM window before hazardous commands)

### Basic diagnostics

```bash
python3 kartctl.py --port /dev/ttyACM0 ping
python3 kartctl.py --port /dev/ttyACM0 status
python3 kartctl.py --port /dev/ttyACM0 help-cmd
```

### Safety / validation

```bash
# Required by test workflow before live drive tests
python3 kartctl.py --port /dev/ttyACM0 validate bringup --profile bench

# Always available emergency safe command
python3 kartctl.py --port /dev/ttyACM0 safe
```

### Hazardous controls (arm-gated)

```bash
python3 kartctl.py --port /dev/ttyACM0 --arm-seconds 2 throttle --percent 5
python3 kartctl.py --port /dev/ttyACM0 --arm-seconds 2 contactor --state on
python3 kartctl.py --port /dev/ttyACM0 --arm-seconds 2 speed --mode high
```

Dry-run examples:

```bash
python3 kartctl.py --dry-run --arm-seconds 2 throttle --percent 10
python3 kartctl.py --dry-run output --name reverse --state on
```

## `can_tool.py`

```bash
python3 can_tool.py --port /dev/ttyACM0 tx --id 0x123 --data 11223344
python3 can_tool.py --port /dev/ttyACM0 poll --max 10
```

`tx` auto-arms before sending; use `--dry-run` to preview commands.

## `esc_tool.py`

```bash
python3 esc_tool.py --port /dev/ttyACM0 read --max 64
python3 esc_tool.py --port /dev/ttyACM0 --arm-seconds 2 write --hex A55A0102
python3 esc_tool.py --port /dev/ttyACM0 watch --duration 10 --interval 0.2
```

## Recommended Bring-up Order

1. Run Pi diagnostics in `../raspberry-pi/`.
2. Run `kartctl validate bringup --profile bench`.
3. Use only low-risk commands (`status`, `hall`, `safe`) until interlock checks pass.
4. Move to arm-gated commands on a secured bench setup.
