from __future__ import annotations

import time
from collections.abc import Callable
from datetime import datetime, timezone

from adv_helper.core.messages import RequestMessage, ResponseMessage, Result


def sample_time() -> tuple[int, int]:
    # Derive both fields from one instant; recompute local offset after timezone changes.
    epoch_ms = time.time_ns() // 1_000_000
    local = datetime.fromtimestamp(epoch_ms / 1000, timezone.utc).astimezone()
    offset = local.utcoffset()
    if offset is None:
        raise ValueError("local UTC offset unavailable")
    return epoch_ms, int(offset.total_seconds() // 60)


class TimeModule:
    def __init__(self, sample: Callable[[], tuple[int, int]] = sample_time) -> None:
        self._sample = sample

    async def handle(self, request: RequestMessage) -> ResponseMessage:
        epoch_ms, offset = self._sample()
        if (type(epoch_ms) is not int or not 0 <= epoch_ms <= 2**63 - 1
                or type(offset) is not int or not -720 <= offset <= 840):
            raise ValueError("invalid system time sample")
        return ResponseMessage("response", request.action_id, request.exec_id,
                               Result("OK", "time loaded", {
                                   "epochMilliseconds": epoch_ms,
                                   "utcOffsetMinutes": offset,
                               }))
