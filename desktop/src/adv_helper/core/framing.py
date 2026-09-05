from __future__ import annotations

import time
from collections.abc import Callable

from .messages import MAX_JSON_BYTES


class FrameError(ValueError):
    pass


class FrameTooLargeError(FrameError):
    pass


class FrameTimeoutError(FrameError):
    pass


class JsonlBuffer:
    def __init__(
        self,
        *,
        max_json_bytes: int = MAX_JSON_BYTES,
        timeout_seconds: float = 5.0,
        clock: Callable[[], float] = time.monotonic,
    ) -> None:
        self._max_json_bytes = max_json_bytes
        self._timeout_seconds = timeout_seconds
        self._clock = clock
        self._buffer = bytearray()
        self._started_at: float | None = None

    def feed(self, chunk: bytes) -> list[bytes]:
        self.check_timeout()
        if not chunk:
            return []
        if not self._buffer:
            self._started_at = self._clock()
        # Accumulate bytes until LF so a multi-byte UTF-8 character can span BLE packets.
        self._buffer.extend(chunk)
        lines: list[bytes] = []
        while True:
            try:
                newline = self._buffer.index(0x0A)
            except ValueError:
                break
            line = bytes(self._buffer[:newline])
            del self._buffer[: newline + 1]
            if len(line) > self._max_json_bytes:
                self.clear()
                raise FrameTooLargeError(f"JSON line exceeds {self._max_json_bytes} bytes")
            if line:
                lines.append(line)
            self._started_at = self._clock() if self._buffer else None
        if len(self._buffer) > self._max_json_bytes:
            self.clear()
            raise FrameTooLargeError(f"JSON line exceeds {self._max_json_bytes} bytes")
        return lines

    def check_timeout(self) -> None:
        if self._started_at is None:
            return
        if self._clock() - self._started_at >= self._timeout_seconds:
            self.clear()
            raise FrameTimeoutError("incomplete JSON line timed out")

    def clear(self) -> None:
        self._buffer.clear()
        self._started_at = None

    @property
    def buffered_bytes(self) -> int:
        return len(self._buffer)
