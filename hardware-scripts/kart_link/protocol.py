"""Pure parsers for kart firmware response lines.

Transport (write/read/timeout) lives in ``kart_link.transport``; this module
only contains stateless functions that turn firmware text into structured
data.
"""

from __future__ import annotations


def parse_status(line: str) -> dict:
    """Turn ``OK STATUS k=v k=v ...`` into a dict.

    Falls back to ``{"raw": line}`` if the prefix doesn't match. Comma-separated
    integer values become lists; other numeric values become ``int`` or
    ``float`` where parseable; everything else stays as ``str``.
    """
    if not line.startswith("OK STATUS"):
        return {"raw": line}
    out: dict[str, str | float | int | list[int]] = {}
    for tok in line.split()[2:]:
        if "=" not in tok:
            continue
        k, v = tok.split("=", 1)
        if "," in v:
            try:
                out[k] = [int(x) for x in v.split(",")]
                continue
            except ValueError:
                pass
        try:
            out[k] = int(v)
            continue
        except ValueError:
            pass
        try:
            out[k] = float(v)
            continue
        except ValueError:
            pass
        out[k] = v
    return out
