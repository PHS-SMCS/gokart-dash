"""Shared kart-protocol transport and helpers for host and Pi tooling.

Consumers add this stanza at the top of their script to import without an
install step::

    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
    from kart_link import KartLink, normalize_hex_bytes  # ...

The path manipulation is cwd-independent so systemd-launched services keep
working regardless of WorkingDirectory=.
"""

from __future__ import annotations

from .errors import KartConnectionError, KartProtocolError, KartTimeoutError
from .hex import normalize_hex_bytes
from .protocol import parse_status
from .transport import CommandResult, KartLink

__all__ = [
    "CommandResult",
    "KartConnectionError",
    "KartLink",
    "KartProtocolError",
    "KartTimeoutError",
    "normalize_hex_bytes",
    "parse_status",
]
