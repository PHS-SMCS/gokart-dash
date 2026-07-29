# Hardware & Open Items

This page consolidates everything that is **not fully captured in the software
repository** — physical hardware details, external configuration, and features
that are stubbed or planned. Each item is also flagged inline on its feature page.
These are documented as **gaps to fill**, not guessed at.

!!! info "How to read this page"
    - **🔧 Physical / hardware** — a real-world detail (wiring, part, mounting) the
      code can't know. Confirm against the kart and the
      [Mainboard repo](https://github.com/PHS-SMCS/SMCSKart-Mainboard).
    - **📡 External config** — set on a device outside the repo (the R/C
      transmitter, the FarDriver app).
    - **🚧 Not implemented** — planned or stubbed in software; no working feature
      today.
    - **❓ Doc discrepancy** — the repo's own docs disagree; confirm which is right.

## Remote control (CRSF)

- **📡 Transmitter & receiver models.** The specific radio and receiver used on the
  current kart are not confirmed in the repo (older notes mention a BetaFPV
  transmitter and a SuperX/ELRS receiver). → *Which TX and RX are actually on the
  kart?*
- **📡 Binding procedure.** The physical steps to pair the transmitter and receiver
  are not documented. → *What is the bind procedure?*
- **📡 Transmitter mixer / channel map.** How each physical control (left stick,
  SA, SD, SE, SF) is assigned to CH1–CH16 is configured on the radio. The firmware
  defaults (ELRS AETR) are confirmed working, but the radio-side setup steps are
  not recorded. → *Document the mixer/channel assignment on the radio.*
- **🔧 Receiver wiring.** Beyond "receiver TX → Teensy pin 15 (Serial3), plus 5 V
  and ground," the connector/harness is not documented. → *Confirm the receiver
  wiring to the mainboard Transceiver connector.*
- **📡 Receiver failsafe = "No Pulses".** Required for the dead-man to work
  (a hold-last-value failsafe defeats it). → *Confirm the receiver is set to No
  Pulses.*

See [Remote Control (CRSF)](inputs/remote-crsf.md).

## Steering hardware

- **❓ Pot pin — GPIO36 vs GPIO32.** The firmware and current bring-up notes use
  **GPIO36**; some older docs say GPIO32. → *Confirm the physical pot-wiper pin.*
- **❓ Talon PWM pin — GPIO15 vs GPIO25.** The firmware uses **GPIO15**; the plan
  and CAN notes say GPIO25. → *Confirm the physical PWM pin (GPIO15 per code).*
- **🔧 Gearbox / motor / pot mechanicals.** The gearbox ratio (documented 50:1), the
  CIM motor, and the pot's mounting and total mechanical travel are physical facts
  not otherwise verifiable. → *Confirm gearbox ratio, motor, pot mounting/travel.*
- **🔧 Talon SRX mode.** Must be in PWM mode with no CAN/RoboRIO owner (FRC-unlock
  if previously used on a RoboRIO). → *Confirm the Talon is PWM-configured.*
- **🔧 CAN termination.** 120 Ω at exactly the two bus ends. → *Confirm termination.*

See [Steering](steering/overview.md).

## Traction / power

- **🔧 Precharge & contactor hardware.** The precharge resistor's value/wattage, the
  main contactor part, and whether a bus-voltage sense line is wired (the code
  supports one but assumes `has_bus_sense = false`) are physical. The firmware's
  timings (4 s precharge, 5 s cap, 10 s cooldown) protect the resistor but assume a
  matching part. → *Confirm the resistor, contactor, and any bus-sense line.*
- **🔧 Hardware e-stop.** Its wiring, what exactly it interrupts, and its placement
  are physical and independent of software. → *Document the e-stop.*
- **📡 FarDriver ND721000 app configuration.** Motor direction, the Low/Med/High
  speed-profile definitions, the regen brake level, and the SPD-pulse settings
  (2→6 pulses, "isolated pulse") are set in the FarDriver app, not the repo. →
  *Record the ESC parameter set.*

See [Arming · Precharge · Contactor](drive/precharge-contactor.md) and
[Gearshifting](drive/gearshift.md).

## Speedometer

- **🔧 Road calibration.** `HALL_MPH_PER_HZ` (≈0.057) is a provisional geometry
  estimate; whether "6 speed pulses" is per motor-rev / electrical-cycle / wheel-rev
  is unconfirmed and changes the factor significantly. The dial tracks speed
  proportionally but the magnitude is provisional until calibrated by driving a
  measured distance. → *Calibrate and set `HALL_MPH_PER_HZ`.*
- **🔧 SPD → pin 22 wiring.** The mainboard pin map does not assign the ESC SPD line
  (pin 13) to a Teensy pin; SPD → pin 22 appears to be a direct bench wire. →
  *Confirm the physical SPD wiring.*

See [Speedometer](drive/speedometer.md).

## Dashboard data with no live source

- **🚧 Battery volts / amps, ESC RPM, temperatures.** The firmware sends
  zeros/unknown for these; there is no ESC-serial telemetry parser or BMS reader.
  The dashboard's battery dial and RPM readout show real values only in **Sim**
  mode. → *Implement the FarDriver ESC-serial telemetry parser and/or the BMS
  reader to make these live.*
- **🚧 BMS (JKBMS).** Planned over RS485; a `bms_probe.py` and integration do not
  exist yet.
- **🚧 IMU (MPU6050).** A `pi/imu_probe.py` exists, but the IMU is not folded into
  the live telemetry or shown on the dashboard.

See [Telemetry Pipeline](dash/telemetry.md).

## Camera

- **🚧 Camera tab is a placeholder.** No camera software exists (no dashboard
  component, no video pipeline, no firmware handling). The board provides an **FPV
  camera UART port on Teensy Serial8 (pins 34/35)**, but nothing drives it. →
  *Specify the camera hardware, the video transport to the web dashboard, and
  whether Serial8 is used for camera control.*

See [Camera Tab](dash/tab-camera.md).

## Map & connectivity

- **🔧 OpenStreetMap tiles need internet.** The map's base imagery loads from
  `tile.openstreetmap.org`; on an isolated kart network the tiles won't load
  (marker/trail still work). → *Provide internet or a local tile cache if map
  imagery is needed on the kart.*
- **🔧 GPS antenna / mounting.** NEO-M9N fix quality depends on antenna placement
  and sky view (physical).
- **🔧 Pi network access.** Hostname/IP/credentials for reaching the Pi depend on
  the deployment network and are not fixed in the repo.

See [Map Tab](dash/tab-map.md) and [Pi Kiosk Deployment](ops/deployment.md).

## Canonical hardware reference

- **🔧 Mainboard revision & pin map.** The authoritative board source is the
  separate KiCad repo **[PHS-SMCS/SMCSKart-Mainboard](https://github.com/PHS-SMCS/SMCSKart-Mainboard)**.
  The [pin-map mirror](SMCSKart-Mainboard/README.md) here is for software
  convenience; if the board and the mirror disagree, **the board wins**.
