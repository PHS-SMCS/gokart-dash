"""Linux joystick (js_event) parsing helpers.

Event format per ``include/uapi/linux/joystick.h``:
    __u32 time   (ms timestamp since open)
    __s16 value
    __u8  type   (0x01 button, 0x02 axis; 0x80 bit set on init events)
    __u8  number
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

JS_EVENT_FMT = "IhBB"
JS_EVENT_SIZE = struct.calcsize(JS_EVENT_FMT)

JS_EVENT_BUTTON = 0x01
JS_EVENT_AXIS = 0x02
JS_EVENT_INIT = 0x80


@dataclass(frozen=True)
class JsEvent:
    time_ms: int
    value: int
    is_button: bool
    is_axis: bool
    is_init: bool
    number: int


def parse_event(chunk: bytes) -> JsEvent | None:
    """Parse one js_event from a fixed-size byte chunk. Returns None on short read."""
    if len(chunk) != JS_EVENT_SIZE:
        return None
    time_ms, value, ev_type, number = struct.unpack(JS_EVENT_FMT, chunk)
    is_init = bool(ev_type & JS_EVENT_INIT)
    base = ev_type & ~JS_EVENT_INIT
    return JsEvent(
        time_ms=time_ms,
        value=value,
        is_button=base == JS_EVENT_BUTTON,
        is_axis=base == JS_EVENT_AXIS,
        is_init=is_init,
        number=number,
    )
