# Camera Tab

!!! warning "Not implemented — placeholder"
    The Camera tab is a **placeholder**. There is **no camera software** in the
    project: no camera component in the dashboard, no video pipeline in the bridge,
    and no firmware handling. Selecting the tab shows a generic placeholder view.

## What hardware exists

The mainboard *does* provide a camera-oriented hardware path, but nothing drives
it in software:

- An **FPV camera UART port** is wired to the Teensy's **Serial8 (pins 34/35)** —
  see the [Mainboard Pin Map](../SMCSKart-Mainboard/README.md). The board comment
  describes it as "UART control for FPV camera."

## What would be needed to make it real

This is entirely unbuilt, so the following are open design questions rather than
documentation of anything that exists:

!!! note "Open items for a real camera feature — please specify"
    - The **camera hardware** itself (an analog FPV camera + receiver? a USB/CSI
      camera on the Pi? an IP camera?) — the mainboard's UART port suggests an FPV
      unit with UART control, but the actual camera is not specified in the repo.
    - The **video transport** to the dashboard (the dashboard is a web app, so it
      would need an MJPEG/WebRTC/HLS stream from somewhere on the Pi).
    - Whether the Teensy's Serial8 port is used for **camera control** (OSD,
      channel) versus the video path.

    See [Hardware & Open Items](../hardware-open-items.md). Let the maintainer know
    the intended camera setup and this page can be replaced with a real feature
    description.
