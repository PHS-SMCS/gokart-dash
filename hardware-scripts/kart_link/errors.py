"""Exception types raised by the kart link transport."""

from __future__ import annotations


class KartProtocolError(RuntimeError):
    """Firmware returned an `ERR ...` response."""


class KartTimeoutError(TimeoutError):
    """No terminal `OK`/`ERR` line observed before timeout."""


class KartConnectionError(ConnectionError):
    """Serial port could not be opened or has disconnected."""
