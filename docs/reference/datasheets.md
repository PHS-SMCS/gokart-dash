# Component Datasheets

Manufacturer datasheets and manuals for the major components on the kart, kept
with the docs so the reference is version-controlled and available offline. These
are **downloads** — click to open the PDF.

!!! info "What's here and what isn't"
    This page collects the human-readable **datasheets and manuals** that map to a
    documented part. **3D models / CAD** (STEP, SLDPRT, STL) are *not* here — they
    live in the separate **[SMCSKart-CAD](https://github.com/PHS-SMCS/SMCSKart-CAD)**
    repo, which will grow to hold the full go-kart assembly. The authoritative board
    source remains the [SMCSKart-Mainboard](https://github.com/PHS-SMCS/SMCSKart-Mainboard)
    KiCad repo.

## Traction & power

| Component | What it is | Datasheet |
|---|---|---|
| **FarDriver NS controller** | The traction **ESC**. See the [FarDriver ESC settings](esc-fardriver.md) page for the app parameter set. | [User Manual (PDF, 3.4 MB)](PDF/ND721000/FarDriver_IP67_Controller_User_Manual_NS.pdf) · [A4203-H spec (PDF, 0.4 MB)](PDF/ND721000/A4203-H-Specification.pdf) |
| **SOTION FW07 motor** | The 72 V 15 kW traction motor driven by the ESC. | [Datasheet (PDF, 5.8 MB)](PDF/FW07/FW07-72V-15KW.pdf) |
| **JK Smart BMS** | Battery management for the 24S pack; the planned source of live pack telemetry (see [Telemetry](../dash/telemetry.md)). | [Instruction manual (PDF, 9.6 MB)](PDF/BMS/JK-Smart-BMS-with-Active-Balancer-Instruction-EN.pdf) |
| **URB_LD-30WR3** | Isolated DC-DC converter module on the mainboard (logic power). | [Datasheet (PDF, 2.5 MB)](PDF/Mainboard/URB_LD-30WR3-Spec.pdf) |
| **IRLZ44N** | Logic-level N-MOSFET used for low-side switching on the mainboard. | [Datasheet (PDF, 0.5 MB)](PDF/Mainboard/IRLZ44N-Spec.pdf) |

## Steering

| Component | What it is | Datasheet |
|---|---|---|
| **CTRE Talon SRX** | The steering motor controller, driven by [servo PWM](../steering/talon-pwm.md) from the Steervo. | [User's Guide (PDF, 1.7 MB)](PDF/Talon-SRX/Talon%20SRX%20User's%20Guide.pdf) · [Outline drawing (PDF, 0.3 MB)](PDF/Talon-SRX/217-8080-Drawing-20150120.PDF) |

## Mainboard signal path

| Component | What it is | Datasheet |
|---|---|---|
| **MCP4725 DAC** | 12-bit I²C DAC that synthesizes the analog [throttle voltage](../drive/throttle.md) for the ESC's TPS line. | [Datasheet (PDF, 1.5 MB)](PDF/Mainboard/MCP4275-DAC-Spec.pdf) |
| **MCP2562 CAN transceiver** | Physical-layer transceiver for the Teensy ⇄ Steervo [CAN link](../steering/can-link.md). | [Datasheet (PDF, 0.8 MB)](PDF/Mainboard/MCP2562-Spec.pdf) |
| **MPU6050 IMU** | 6-axis IMU on the mainboard. A probe exists but it is **not yet** in live telemetry — see [Hardware & Open Items](../hardware-open-items.md). | [Datasheet (PDF, 1.6 MB)](PDF/Mainboard/MPU6050-Spec.pdf) |
| **Ethernet connector** | Board Ethernet jack. | [Spec (PDF, 0.6 MB)](PDF/Mainboard/Ethernet-Connector-Spec.pdf) |

## GPS

| Component | What it is | Datasheet |
|---|---|---|
| **u-blox NEO-M9N** | The GPS module behind the [Map tab](../dash/tab-map.md). | [Datasheet (PDF, 0.7 MB)](PDF/Mainboard/GPS/NEO-M9N-00B_DataSheet_UBX-19014285.pdf) |

!!! note "More NEO-M9N documents are in the repo"
    The repo also holds the u-blox integration manual, hookup guide, and product
    summary under `docs/reference/PDF/Mainboard/GPS/`. They are **not published to
    this site** (redundant with the datasheet and large); browse them in the
    [repository](https://github.com/PHS-SMCS/gokart-dash/tree/main/docs/reference/PDF/Mainboard/GPS)
    if needed. The `u-blox8-M8` protocol spec in that folder covers the older **M8**
    series, not our M9N.
