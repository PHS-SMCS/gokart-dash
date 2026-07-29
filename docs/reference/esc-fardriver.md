# FarDriver ESC — Motor Controller & Advanced Settings

The traction motor is driven by a **FarDriver NS-series controller** (the ESC).
It is *not* on the software stack's control surface — the Teensy talks to it only
through discrete lines (throttle DAC voltage on TPS, brake/reverse/gear lines to
ground, SPD pulses back). Everything the ESC *itself* does — current limits, the
three speed profiles, regen behaviour, protections, the speedometer output — is
configured in the **FarDriver mobile app**, not in this repository.

This page is the complete enumeration of every parameter exposed in the app's
Advanced Settings page, as observed on our controller, so those "external config"
settings are captured somewhere version-controlled.

!!! info "Where these settings live"
    The Teensy firmware cannot read or write any of these values — they live in the
    ESC's own flash and are only reachable through the FarDriver app over Bluetooth.
    Related software-side pages: [Throttle](../drive/throttle.md),
    [Brake](../drive/brake.md), [Reverse & Gearshifting](../drive/gearshift.md),
    and [Speedometer](../drive/speedometer.md).

!!! abstract "Datasheets"
    [FarDriver NS User Manual (PDF, 3.4 MB)](PDF/ND721000/FarDriver_IP67_Controller_User_Manual_NS.pdf)
    · [SOTION FW07 motor (PDF, 5.8 MB)](PDF/FW07/FW07-72V-15KW.pdf)
    — see all component datasheets on the [Datasheets](datasheets.md) page.

## Accessing the Advanced Settings

The full parameter set is **hidden by default** in the FarDriver app. To reveal it:

1. Open the FarDriver app and connect to the controller over Bluetooth.
2. Go to **`Paras`** (parameters).
3. Tap **`Open Pro`** to unlock the professional / advanced parameter pages.

Once unlocked, the pages enumerated below (Parameters, Ratios, Energy Regenerate,
Functions, Display, Protect, PID, Product, FixedParas) become visible.

!!! warning "Unlocking the full app"
    Getting into the full-featured version of the FarDriver app is not obvious and
    is poorly documented online. The screen recording below is a rare, working
    reference for the unlock procedure — keep it with the docs.

<video controls preload="metadata" style="width: 100%; max-width: 720px; height: auto;">
  <source src="../unlock-fardriver.mp4" type="video/mp4">
  Your browser cannot play embedded video.
  <a href="../unlock-fardriver.mp4">Download the FarDriver unlock walkthrough (MP4)</a>.
</video>

## How to read this document

Every setting visible in the app is listed. Descriptions are marked with a
confidence level, and **a blank description means we do not know what the
setting does.** Blanks are deliberate. Do not fill them in with guesses —
add to this document only when a behavior has been confirmed on the kart or
in vendor documentation.

| Confidence | Meaning |
|---|---|
| **Confirmed** | Stated in the SunPole NS manual, or verified empirically on this kart. |
| **Inferred** | Strong reasoning from naming, observed values, or standard motor-control practice. Plausible but **unverified** — verify before relying on it. |
| *(blank)* | Unknown. Not guessed at. |

Values shown are as configured on our kart as of 2026-07-28. They are a
snapshot, not a recommendation, except where noted.

---

## Header

| Setting | Value | Description | Confidence |
|---|---|---|---|
| CostumCode | ND | Controller model / customer code string. Reads "ND" despite the unit being an NS-series controller. | Inferred |
| MorseCode | 000000 | Stored morse-code sequence. The manual documents using brake on/off pulses as a "morse code" to trigger auto-learn and reverse motor direction without the app. | Inferred |
| Date / Time | 2026-7-28 / 17:0:4 | Controller real-time clock. | Confirmed |

---

## Parameters (motor)

| Setting | Value | Description | Confidence |
|---|---|---|---|
| AngleDetect | 1-Encoder | Rotor position feedback source. Manual states the controller supports 60° hall, 120° hall, and encoder types. | Confirmed |
| TempSensor | 6-NTC10K | Motor temperature sensor type. Manual lists PTC, NTC, KTY84-130, 83-122, and 84-121 as supported. | Confirmed |
| PhaseOffset | 202 | Electrical angle offset between encoder zero and rotor magnetic zero. Set by the auto-learn procedure. | Inferred |
| PolePairs | 5 | Motor pole pairs (10 magnetic poles). Used to convert electrical frequency to mechanical RPM. | Confirmed |
| Motor Direction | 1 | Which rotation direction the controller treats as forward. Manual notes this is also changeable via morse code. | Confirmed |
| RatedSpeed | 6684RPM | Motor nameplate rated speed. | Inferred |
| RatedVoltage | 72V | Motor nameplate rated voltage. | Inferred |
| RatedPower | 9000W | Motor nameplate rated power. **Note:** our SOTION FW07 is a 15 kW motor; this field is set to 9000 W. See "Settings needing attention" below. | Inferred |
| MaxSpeed | 12000RPM | Upper motor speed limit. | Inferred |
| BackSpeed | 400RPM | Speed limit while in reverse. Manual confirms forward and reverse speeds are separately configurable. | Confirmed |
| MaxLineCurr | 500A | Maximum battery bus (DC) current. Manual confirms bus current is user-configurable. | Confirmed |
| MaxPhaseCurr | 1000A | Maximum motor phase (AC) current. Manual confirms phase current is user-configurable. | Confirmed |
| ThrottleResponse | 0-Line | Shape of the throttle input-to-output curve; "Line" = linear. | Inferred |
| Throttle Acc Step | 128 | Rate limit on how fast commanded torque may increase (acceleration ramp). | Inferred |
| BoostLineCurr | 500A | Bus current limit while the Boost function is active. | Inferred |
| BoostPhaseCurr | 1000A | Phase current limit while the Boost function is active. | Inferred |
| PhaseExchange | No Exchange | Swaps two motor phases in software, reversing rotation without rewiring. | Inferred |
| ECOAccCoeff | 8 | Acceleration coefficient applied in Economy mode. Manual confirms Economy mode speed is adjustable. | Inferred |
| Weak Character | 0-Fast | Field-weakening engagement character. Manual confirms field weakening is supported. | Inferred |
| WeakResponse | 0 | | |
| Throttle Dec Step | 128 | Rate limit on how fast commanded torque may decrease (deceleration ramp). | Inferred |
| Release Throttle | 0 | | |
| Throttle Low | 0.45V | Throttle voltage below which input is treated as zero ("TPS Dead Low" in the manual). Below this, the throttle is considered released. Manual error code 2 describes raising this if the controller falsely reports throttle fault at idle. | Confirmed |
| Throttle High | 4.05V | Throttle voltage treated as full scale ("TPS Dead High"). Manual error code 2 describes raising this if max throttle voltage exceeds the threshold. | Confirmed |

---

## Ratios in Speed

A 19-point lookup table (500–9000 RPM in 500 RPM steps) setting the **current
limit as a percentage of maximum** at each motor speed. Manual: "Current amount
can be adjusted under different speeds in the user program." **Confirmed.**

Current values: 100% at 500–1000 RPM, tapering through 90/80/70/60/55/53/51/48/46/44%
to 35% at 6500, then 5% at 7000, 1% at 7500–8000, and 0% at 8500–9000 RPM.

| Setting | Value | Description | Confidence |
|---|---|---|---|
| LD | 900 | Believed to be d-axis inductance, used by the FOC current loop. **Caution:** for most interior-PM motors LQ exceeds LD, and here the reverse is true — so either the labels differ from convention or the units are not what we assume. Do not rely on this reading. | Inferred |
| LQ | 329 | Believed to be q-axis inductance. Same caveat as LD. | Inferred |
| FAIF | 128 | | |
| LimitSpeed | 3500RPM | Speed cap enforced when the SpeedLimit input is active. | Inferred |

---

## Ratios in Gear (three-speed mode)

Manual: "Under three speed modes, you can configure the speed limiting and
current limitings for each speed mode." **Confirmed** as a group.

| Setting | Value | Description | Confidence |
|---|---|---|---|
| LowSpeedLineRatio | 25% | Bus current limit in low gear, as % of MaxLineCurr. | Confirmed |
| MidSpeedLineRatio | 50% | Bus current limit in medium gear, as % of MaxLineCurr. | Confirmed |
| LowSpeedPhaseRatio | 50% | Phase current limit in low gear, as % of MaxPhaseCurr. | Confirmed |
| MidSpeedPhaseRatio | 75% | Phase current limit in medium gear, as % of MaxPhaseCurr. | Confirmed |
| LowSpeed | 3500RPM | Speed cap in low gear. | Confirmed |
| MiddleSpeed | 4500RPM | Speed cap in medium gear. | Confirmed |

High gear has no ratio entries; it uses the unmodified Max values.

---

## Energy Regenerate

| Setting | Value | Description | Confidence |
|---|---|---|---|
| StopBackCurr | 2A | Regen current commanded as the vehicle approaches standstill. Low value gives a smooth rollout instead of a lurch. | Confirmed |
| MaxBackCurr | 15A | Ceiling on regen (charge) current. Sets the *magnitude* of regen braking; the RPM table sets its *shape*. Verified on this kart: raising this from 4 A produced perceptible braking. | Confirmed |
| Batt RatedCapacity | 105AH | Pack capacity, used for state-of-charge estimation. Corrected from a default of 20 AH to match our 24S 105 Ah Narada pack. | Confirmed |
| FreeThrottle | 0 | | |
| Brake Voltage | 0.35V | Threshold for an **analog** brake sensor. Only active in Brake modes 5-LinearBrake and 6-P+LineBrake. Inert on our kart, which uses a digital contact-to-ground brake switch on pin 21. | Confirmed |

**Regen strength table** — 19 points, 500–9000 RPM, regen as % of MaxBackCurr.
Verified empirically: with all entries at 0%, no regen occurs regardless of any
other setting. Current values ramp 0% at 500 RPM through 10/15/20/25/30/35% to
40% at 4000 RPM, holding 40% to 9000 RPM. **Confirmed.**

Regen torque is proportional to speed and **falls to zero at zero RPM.** Regen
cannot hold a stationary vehicle. See the Park settings.

---

## Functions — input pin assignments

Each of these binds a logical function to a physical input pin on the 30-way
connector. `13-Invalid` means unassigned; `0-NC` means not connected. Per the
manual, these inputs activate by being connected to GND. **Confirmed** as a group.

| Setting | Value | Function assigned to a pin |
|---|---|---|
| Boost Pin | 13-Invalid | Temporary power boost |
| Cruise Pin | 13-Invalid | Cruise control engage |
| SideStand Pin | 13-Invalid | Side-stand-down interlock |
| Pause Pin | 0-NC | |
| Forward Pin | 13-Invalid | Forward direction select |
| Backward Pin | 4-PIN8 | Reverse direction select (pin 8, Brown-White, REV) |
| Highspeed Pin | 2-PIN3 | High gear select (pin 3, Yellow-White) |
| LowSpeed Pin | 1-PIN2 | Low gear select (pin 2, Blue-White) |
| Charge Pin | 13-Invalid | Charger-present detect |
| Anti-theft Pin | 6-PIN14 | Anti-theft trigger (pin 14, Black-White, FW) |
| Seat Pin | 13-Invalid | Seat occupancy interlock |
| SpeedLimit Pin | 0-NC | Engages LimitSpeed |
| Switch Voltage Pin | 13-Invalid | |
| Repair Pin | 13-Invalid | |

| Setting | Value | Description | Confidence |
|---|---|---|---|
| BoostTime | 45s | Maximum continuous duration of the Boost function. | Inferred |
| BoostRelease | 90s | Cooldown required after Boost before it may re-engage. | Inferred |
| HighLowSpeed | 4-Button3SpeedLow | How the high/low speed inputs are interpreted (momentary buttons vs. latching switch, and which state is the default). | Inferred |
| Puse RE | 0-Invalid | | |
| EmptyRun | 0 | | |
| SlowDown | 4 | Related to SlowDownRpm and SlowDownCoeff on the Product page. | Inferred |

---

## Functions — behavior modes

| Setting | Value | Description | Confidence |
|---|---|---|---|
| Gear | 1-Default D | Gear selected at power-on. | Inferred |
| Brake | 2-P+StopGnd | Action taken when the brake input goes active. Options: `0-StopWhenGround` (cut torque, phases pulled low), `1-StopWhenFloat` (cut torque, phases float — pure coast), `2-P+Stop`, `3-P+StopGnd`, `4-P+Disabled` (the `P+` variants add Park), `5-LinearBrake` and `6-P+LineBrake` (braking proportional to an analog brake signal), `7-Disabled` (ignore the input). **Note:** the app's picker lists `2-P+Stop` and `3-P+StopGnd` as distinct, but after selecting one the display reads `2-P+StopGnd` — the index-to-label mapping is inconsistent. Verify behavior on the kart rather than trusting the string. | Confirmed |
| PC13 | 0-NomalResponse | Named after an STM32 MCU pin; the app is exposing an internal signal name. Purpose unknown. | |
| Park | 1-SwitchPark | Zero-speed hold mode. Options: `0-ReversePark`, `1-SwitchPark` (expects a dedicated park switch input — **note no park pin is assigned in our Functions list, so this may be inert as configured**), `3-SlowDownPark` (engages as the vehicle comes to rest), `2-Disabled`. | Confirmed |
| Follow | 2-EABSWhenBrake | Master trigger for EABS (regenerative braking). Options: `0-Follow Enabled`, `1-Disabled` (**no regen at all**), `2-EABS when brake valid` (regen tied to the brake switch), `3-EABS when Release Throttle` (regen on throttle release). Verified: with this at `1-Disabled`, pressing the brake produces no regen even with a populated regen table. | Confirmed |

---

## Display

| Setting | Value | Description | Confidence |
|---|---|---|---|
| Speed Pulses | 1 | Pulses emitted per wheel revolution on the SPD output (Light-Blue wire). | Confirmed |
| SpdPulseNum | 40459 | Fixed-point scaling constant for speed calculation. Appears to be derived from wheel circumference rather than user-set: it did not change when Speed Pulses was changed from 6 to 1, and `40459 × 1.485 m ≈ 60,100`, consistent with a stored `60000 / circumference` term. **Unverified** — confirm by changing the wheel dimension fields and checking whether this value updates on its own. | Inferred |
| SpeedoMeter | 2-IsolatedPulse | Speedometer output type. Manual: "Isolated type pulse signal or CAN communication protocol." | Confirmed |
| AnalogSpeedCoeff | 10000RPM | Full-scale RPM for the **analog** speedometer output (Purple wire, pin 9). Unrelated to the pulse output path. | Inferred |
| CAN | None | CAN protocol selection. Manual: CAN is an optional function; a separate manual version covers it. | Confirmed |
| CAN Detect | 150ms | CAN communication timeout. | Inferred |
| CAN Baud | 0-250K | CAN bit rate. Manual confirms configurable baud rate. | Confirmed |
| TorqueCoeff | 748 | | |
| Step | 1-0.9ms | | |
| Stop | 2-124ms | | |
| SpecialFrame | 21 | | |
| PULSE | 0 | | |
| SQH | 0 | | |
| DATA0 | 8 | | |
| DATA1 | 97 | | |
| SEC0–SEC7 | all 0 | | |
| PPosition | 1 | | |
| BCPosition | 0 | | |
| HbarPosition | 0 | | |
| FDPosition | 8 | | |
| CurrentCoeff | 64 | | |
| ByteOption | 3 | | |
| WheelWidth | 120 | Tire section width in mm. | Confirmed |
| WheelRatio | 70 | Tire aspect ratio (sidewall height as % of width). | Confirmed |
| WheelR | 12 | Rim diameter in inches. | Confirmed |
| GearRatio | 4 | Motor revolutions per wheel revolution. | Inferred |

`SpecialFrame` through `ByteOption` appear to describe the byte layout of a
serial frame for a specific aftermarket dashboard. We do not use one, and none
of these affect torque, regen, or protection.

**Wheel dimensions:** 120/70-12 describes a scooter tire, ≈473 mm OD, ≈1.485 m
rolling circumference. This does not match our kart's tires and makes every
speed and distance figure the controller reports incorrect. See below.

---

## Protect

| Setting | Value | Description | Confidence |
|---|---|---|---|
| OverVolProtect | 90.7V | Bus voltage at which the controller cuts out on over-voltage (manual error code 5). | Confirmed |
| OverVolRestore | 88.7V | Voltage at which over-voltage protection releases (hysteresis). | Confirmed |
| LowVolProtect | 63V | Bus voltage at which the controller cuts out on under-voltage (manual error code 5). | Confirmed |
| LowVolRestore | 65V | Voltage at which under-voltage protection releases. | Confirmed |
| MotorTempProtect | 160°C | Motor temperature cutoff (manual error code 7). | Confirmed |
| MotorTempRestore | 140°C | Motor temperature at which protection releases. | Confirmed |
| ControllerTempProtect | 100°C | Controller/MOSFET temperature cutoff (manual error code 8). | Confirmed |
| ControllerTempRestore | 80°C | Controller temperature at which protection releases. | Confirmed |
| 0 BattCoeff | 896 | Believed to be the "empty" endpoint for the SOC gauge. If scaled by 1/16 this reads 56.0 V, which is plausible for a 24S pack (2.33 V/cell) but unverified. | Inferred |
| Full BattCoeff | 1250 | Believed to be the "full" endpoint for the SOC gauge. 1/16 scaling gives 78.1 V (3.26 V/cell). Unverified. | Inferred |
| ThrottleLost | 0-Inval | Detection of a disconnected or failed throttle signal. Manual documents accelerator pedal failure protection. | Inferred |
| ThrottleInsert | 256 | | |
| BackP_Time | 18-240s | | |
| ReleaseToSeat | 2s | Related to the Seat Pin interlock. | Inferred |
| BlockTime | 50s | | |
| ParkTime | 10s | Believed to bound how long park hold is maintained before releasing. **Important if true** — the vehicle will begin to roll when it expires. Verify empirically before relying on park on any grade. | Inferred |
| LmtSpdStartCap | 0 | | |
| LmtSpdMinCap | 0 | | |
| LmtSpdMaxCoeff | 128 | | |
| TurtleSpeedCurrCoeff | 53 | | |
| BattSignal | 3-Lithium Battery | Battery chemistry, selecting the voltage-to-SOC curve. | Inferred |
| LowVol Way | 0-Vol2V | | |
| IntRes | 32 | | |
| TempCoeff | 300 | Also appears under FixedParas. See that section. | |

---

## PID Paras

| Setting | Value | Description | Confidence |
|---|---|---|---|
| AN | 16 | | |
| LM | 22en-US | App language/locale setting, not a control parameter. | Inferred |
| StartKI | 4 | Current-loop integral gain in the low output range. | Inferred |
| StartKC | 0 | Current-loop companion gain in the low output range. | Inferred |
| MidKI | 8 | Current-loop integral gain in the mid output range. | Inferred |
| MidKC | 80 | Current-loop companion gain in the mid output range. | Inferred |
| MaxKI | 12 | Current-loop integral gain in the high output range. | Inferred |
| MaxKC | 120 | Current-loop companion gain in the high output range. | Inferred |
| SpeedKI | 9 | Speed-loop integral gain. | Inferred |
| SpeedKP | 10 | Speed-loop proportional gain. | Inferred |
| MOE | 0-Enable | Hardware over-current protection. Manual error code 15: MOE events accumulate in the user program's statistics count, and the manual states MOE protection may be disabled on 84 V and above controllers **once parameters are well tuned** — with an explicit warning that it must not be disabled if the current limit cannot cap maximum speed, since overspeed with deep field weakening will destroy the controller unprotected. **Leave enabled.** | Confirmed |
| CurveTime | 100ms | Sample interval for the app's Curve (data logging) tab. | Inferred |

The KI/KC gain naming is non-standard and the three ranges are assumed to map to
low/mid/high output. Do not tune these without a way to observe current-loop
response.

---

## Product

Most of this page is undocumented. Values shown for completeness.

| Setting | Value | Description | Confidence |
|---|---|---|---|
| ReCurrRatio | 128 | | |
| FwReRatio | 64 | | |
| VolSelectRatio | 106 | | |
| WeakCurrCoeff | 64 | | |
| Re Acc | 64s | | |
| AlarmDelay | 500 | | |
| RelayDelay | 21623ms | | |
| RelayOut | 0 | | |
| BCEnable | 1 | | |
| SeatEnable | 1 | Master enable for the seat-switch interlock, paired with Seat Pin. | Inferred |
| PEnable | 1 | Master enable for the Park function. | Inferred |
| AutoBackPEnable | 0 | | |
| CruiseEnable | 1 | Master enable for cruise control, paired with Cruise Pin. | Inferred |
| EABSEnable | 1 | Master enable for EABS/regenerative braking, paired with Follow. | Inferred |
| PushEnable | 1 | Master enable for a walk/push-assist mode. | Inferred |
| ForseAntiTheft | 0 | | |
| OverSpeedAlarm | 0 | | |
| BrakeStillPark | 0 | Believed to engage Park when the brake is held and the vehicle has come to a stop. The most directly-named control for brake-at-standstill hold. Unverified. | Inferred |
| RememberGear | 1 | Retain the selected gear across power cycles. | Inferred |
| BackEnable | 1 | Master enable for reverse. | Inferred |
| RelayDelay1S | 0 | | |
| 0SpeedSwitch | 0-Invalid | | |
| StartIs | 512 | | |
| FollowSpeed | 0rpm | | |
| Curr-Anti-theft | 0-Invalid | An anti-theft detection variant. | Inferred |
| Anti-theft Pulse | 0-Invalid | An anti-theft detection variant. | Inferred |
| Temp 70 | 0 | | |
| Fast RE | 1 | | |
| InverseTime | 36 | | |
| SlowDownRpm | 512 | | |
| SlowDownCoeff | 10 | | |
| RXD | 0-AF | | |
| LearnThrottle | 24 | Stored value from the auto-learn throttle calibration. | Inferred |
| LearnVolLow | 18432 | Stored value from the auto-learn throttle calibration. | Inferred |
| LearnVolHigh | 24320 | Stored value from the auto-learn throttle calibration. | Inferred |
| DeepWeak | 0-Normal | Deep field-weakening mode selector. Manual warns that overspeed with deep field weakening and no protection will destroy the controller. | Inferred |
| Protocol485 | 0 | | |

---

## FixedParas

| Setting | Value |
|---|---|
| LineCurrCoeff | 640 |
| BattVoltage | 256 |
| PhaseCoeffA | 295 |
| PhaseCoeffB | 295 |
| PhseAZero | 2083 |
| PhaseCZero | 2081 |

| Setting | Value |
|---|---|
| TempCoeff | 300 |
| VoltageCoeff | 256 |

These appear to be factory analog-front-end calibration constants — ADC scaling
for current, voltage, and temperature sensing, plus zero-offset for the phase
current sensors. The `PhseAZero`/`PhaseCZero` values of 2083 and 2081 sit
essentially at the midpoint of a 12-bit ADC (2048), which is what a bipolar
current sensor's zero-current reading should be, and they differ slightly from
each other as per-unit calibration would. **Inferred.**

> **Do not modify anything in FixedParas.** These are calibrated per individual
> controller at the factory. Incorrect values will cause the controller to
> mis-measure current and voltage, defeating every protection that depends on
> those readings. Record them before any firmware update so they can be
> restored.

---

## Settings needing attention

Flagged during review. Not yet resolved.

1. **MaxLineCurr 500A / BoostLineCurr 500A vs. BMS limits.** Our JK BMS is
   configured for roughly 105 A continuous and 200 A burst. A hard launch will
   command current the pack will refuse, tripping BMS over-current protection
   and opening the discharge FET while the kart is moving — instant loss of
   power under way. Reduce to below the BMS burst limit.

2. **Wheel dimensions do not match the kart.** 120/70-12 is a scooter tire
   (≈1.485 m circumference). Kart tires are closer to 0.85 m. All reported speed
   and odometer figures are inflated by roughly 1.75×, which explains observed
   readings like 44 km/h average speed on a kart that has barely moved. Correct
   the three wheel fields, then re-check whether SpdPulseNum updates itself.

3. **RatedPower 9000W vs. a 15 kW motor.** Whether this field limits output or
   is merely informational is unverified.

4. **Park may be inert as configured.** `Park: 1-SwitchPark` expects a dedicated
   park switch input, and no park pin is assigned. `3-SlowDownPark` is more
   likely to engage unaided.

5. **Brake mode label mismatch.** The picker offers `2-P+Stop` and
   `3-P+StopGnd` separately; the display afterward reads `2-P+StopGnd`. Trust
   observed behavior over the displayed string.

---

## Safety note on Park

Park is the only mode that commands torque at zero speed, and it is **not a
parking brake.** It fails open: if the contactor opens, the BMS trips, the
controller faults, ParkTime expires, or the key is switched off, holding force
disappears instantly and without warning. It also drives current into a stalled
motor with no cooling airflow, which is a thermal limit as much as a time limit.

Until a mechanical brake is fitted, the kart must be kept on level ground and
chocked whenever parked or worked on. Park is a convenience for hill starts
layered on top of a mechanical brake — never a substitute for one.

---

## Regen prerequisites

Regenerative braking requires **all** of the following. Any one of them
defeats it:

- `Follow` set to `2-EABSWhenBrake` or `3-EABS when Release Throttle`
- Non-zero entries in the Energy Regenerate RPM table
- `MaxBackCurr` set high enough to be felt (4 A was imperceptible; 15 A was not)
- `EABSEnable` = 1
- No freewheel or one-way clutch in the driveline
- **Charging enabled on the BMS**

That last one is not optional. Regen current returns through the BMS charge
FET. With charging disabled, that current has nowhere to go but the
controller's bus capacitors, and bus voltage rises until something fails. Also
verify the BMS charge over-current limit exceeds peak regen current, or a hard
stop will trip charge OCP and open the FET mid-brake — producing the same load
dump.

The JK will also open the charge FET on its own below 0 °C, since LiFePO4
plates lithium when charged cold. The controller has no way to know this
happened. The robust firmware interlock is therefore to poll the BMS charge-FET
status and inhibit regen whenever that FET is open **for any reason**, which
covers over-voltage, over-current, and low-temperature in a single condition —
rather than inhibiting on pack voltage alone, which only catches the
over-voltage case.
