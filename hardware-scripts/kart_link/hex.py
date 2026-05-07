"""Hex payload normalization shared by host and Pi tooling."""

from __future__ import annotations

import string


def normalize_hex_bytes(value: str, *, max_bytes: int | None = None) -> str:
    """Strip non-hex characters, validate even-length, return uppercased hex.

    Args:
        value: Raw user input (may contain whitespace, separators, etc.).
        max_bytes: Optional cap on payload length, in bytes. CAN frames pass
            ``max_bytes=8``; ESC writes pass ``None`` (no cap).

    Raises:
        ValueError: If the cleaned payload is empty, has odd length, or
            exceeds ``max_bytes``.
    """
    cleaned = "".join(ch for ch in value if ch in string.hexdigits)
    if not cleaned:
        raise ValueError("hex payload is empty")
    if len(cleaned) % 2 != 0:
        raise ValueError("hex payload must contain an even number of nybbles")
    if max_bytes is not None and len(cleaned) > max_bytes * 2:
        raise ValueError(f"hex payload max is {max_bytes} bytes")
    return cleaned.upper()
