# Host Tools

The `tools/` directory holds operator-side command-line utilities for talking to
the Teensy over USB serial from a laptop, plus the on-Pi probes in `pi/`. They are
bench aids, not part of the running kart.

## `tools/` — host serial CLIs

| Tool | Purpose |
|---|---|
| `kartctl.py` | Primary CLI: `ping`, `status`, `safe`, and command wrappers. Common flags: `--port` (default `/dev/ttyACM0`), `--baud`, `--dry-run`. |
| `can_tool.py` | Send/poll raw CAN frames through the Teensy (`tx --id … --data …`, `poll`). |
| `esc_tool.py` | FarDriver ESC serial passthrough (`read`, `write --hex …`, `watch`). |
| `serial_link.py` | Shared serial transport used by the others. |
| `can_link_check.py`, `steer_hello.py` | CAN-link and steering-hello diagnostics. |

Install the one dependency with `pip install pyserial` (or run via
`uv run --with pyserial …`).

!!! warning "Some `kartctl` commands target the legacy firmware surface"
    `kartctl.py` predates the current `kart-core` and includes arm-gated wrappers
    (e.g. `throttle`, `contactor`, `speed` with `--arm-seconds`) that were written
    against the **legacy** firmware's `ARM <seconds>` command. The current
    `kart-core` arms via the **wheel chord** or the **RC SA switch**, not a serial
    arm window, so those hazardous wrappers may not behave as written. The
    read-only commands (`ping`, `status`, `safe`) and the raw serial commands in
    the [UART Protocol](../protocols/uart-protocol.md) are the reliable surface for
    the current firmware.

## `pi/` — on-board probes

Run these on the Pi during bring-up (from `pi/`, in the venv):

| Probe | Checks |
|---|---|
| `i2c_scan.py` | I2C bus scan with expected-device checks. |
| `imu_probe.py` | MPU6050 ID + accel/gyro sample readout. |
| `gps_probe.py` | NEO-M9N probe over I2C or serial. |
| `teensy_uart_probe.py` | Pi ↔ Teensy UART ping/safe test. |
| `wheel_probe.py` | Maps the Hori wheel's axes/buttons on the Pi (Linux `js` values). |
| `wheel_bridge.py` | Legacy fallback: forward Pi-side wheel events to the Teensy over UART. |

See the [Original Bring-up Guide](../bringup.md) for the probe sequence, and
[Bench Bring-up](../ops/bringup.md) for the current firmware procedure.
