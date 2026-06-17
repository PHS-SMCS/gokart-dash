#!/usr/bin/env python3
# PlatformIO pre-build script: teach USBHost_t36's JoystickController about the
# Hori "Racing Wheel Overdrive" (VID 0x0F0D / PID 0x0152).
#
# Why: the bundled USBHost_t36 (v0.2) only claims Xbox One controllers whose
# VID/PID are in its hardcoded table (Microsoft devices only). The Hori wheel
# uses the standard Xbox One GIP interface (class 0xFF/0x47/0xD0) and works fine
# once its VID/PID is in that table — but without it, JoystickController::claim()
# bails at the VID/PID check and the wheel never enumerates as a joystick. This
# is exactly why the project previously HAD to flash from the Arduino IDE (whose
# Teensyduino shipped a USBHost_t36 that knew the wheel). Patching here lets us
# flash from PlatformIO/CLI with the wheel working in production.
#
# This runs before every build, finds the framework's joystick.cpp, and inserts
# the table entry if it is missing (idempotent). It re-applies automatically
# after a toolchain reinstall, and always patches the matching library version
# (no vendored fork to drift from the core).

import os

Import("env")  # noqa: F821  (injected by PlatformIO)

VID_PID = "0x0f0d, 0x0152"
ENTRY = "    { 0x0f0d, 0x0152, XBOXONE, false },  // Hori Racing Wheel Overdrive (added by patch_usbhost_t36.py)\n"
ANCHOR = "pid_vid_mapping[] = {"

pkg_dir = env.PioPlatform().get_package_dir("framework-arduinoteensy")  # noqa: F821
joystick_cpp = os.path.join(pkg_dir, "libraries", "USBHost_t36", "joystick.cpp")

if not os.path.isfile(joystick_cpp):
    print("patch_usbhost_t36: WARNING joystick.cpp not found at %s" % joystick_cpp)
else:
    with open(joystick_cpp, "r") as f:
        src = f.read()
    if VID_PID in src.lower():
        print("patch_usbhost_t36: Hori wheel (0F0D:0152) already present")
    else:
        idx = src.find(ANCHOR)
        if idx == -1:
            print("patch_usbhost_t36: WARNING anchor %r not found; wheel may not "
                  "enumerate (USBHost_t36 layout changed?)" % ANCHOR)
        else:
            insert_at = src.find("\n", idx) + 1
            src = src[:insert_at] + ENTRY + src[insert_at:]
            with open(joystick_cpp, "w") as f:
                f.write(src)
            print("patch_usbhost_t36: added Hori wheel (0F0D:0152) to "
                  "JoystickController VID/PID table")
