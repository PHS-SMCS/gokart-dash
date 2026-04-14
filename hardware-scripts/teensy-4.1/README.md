# Teensy 4.1 Firmware (`kart_controller.ino`)

This firmware exposes a simple line-based command protocol over:

- USB CDC (`Serial`)
- Raspberry Pi UART bridge (`Serial2`, pins 7/8)

It maps commands to documented SMCSKart interfaces (ESC toggles, DAC throttle, CAN, telemetry) and enforces safety interlocks.

## Implemented Hardware Map

- **UART**
  - `Serial1` (pins 0/1): ND721000 ESC serial passthrough
  - `Serial2` (pins 7/8): Pi command/control bridge
- **I2C**
  - `Wire` on pins 18/19: MCP4725 DAC (`0x60`) for throttle output
- **CAN**
  - `CAN1` via MCP2562 transceiver on pins 30/31
- **Digital control outputs** (MOSFET switched, command semantics are active-low at ESC)
  - Reverse (pin 3)
  - Low brake (pin 4)
  - High speed (pin 5)
  - Low speed (pin 6)
  - Cruise (pin 9)
  - Contactor (pin 32)
- **Inputs**
  - Hall pulse input (pin 2, interrupt counted)
  - PPS input (pin 10)
- **PWM**
  - LED strip channels: R=37, G=36, B=33

## Safety Behavior

- Default boot state: all switched outputs off, throttle DAC at minimum, LED off.
- Hazardous commands require `ARM <seconds>` first.
- When arm timer expires, firmware auto-applies `SAFE` state.
- De-asserting outputs (`off`/`0`) and `THROTTLE 0` are always allowed.

## Upload

1. Install Arduino IDE + Teensyduino.
2. Open `kart_controller.ino`.
3. Select **Board: Teensy 4.1**.
4. Build and upload.

## Protocol (one command per line)

Responses always begin with `OK` or `ERR`.

### Core

- `PING`
- `HELP`
- `STATUS`
- `ARM <seconds>`
- `DISARM`
- `SAFE`

### Outputs / drive control

- `OUTPUT <reverse|brake|speed_low|speed_high|cruise|contactor> <on|off>`
- `SPEED <low|medium|high>`
- `REVERSE <on|off>`
- `BRAKE <on|off>`
- `CONTACTOR <on|off>`
- `THROTTLE <0..100>`
- `LED <r> <g> <b>` (0..255)

### Telemetry / buses

- `HALL?`
- `ESC_WRITE <hexbytes>`
- `ESC_READ [max_bytes]`
- `CAN_TX <id> <hexbytes>`
- `CAN_POLL [max_frames]`

## Example Session

```text
PING
OK PONG
ARM 2
OK ARM seconds=2.00
THROTTLE 5
OK THROTTLE pct=5.00 dac=888
SAFE
OK SAFE
```
