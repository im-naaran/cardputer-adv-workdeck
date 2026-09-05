import json
from datetime import datetime, timedelta, timezone
from pathlib import Path

import pytest

from adv_helper.application.time_module import TimeModule, sample_time
from adv_helper.core.messages import ProtocolError, RequestMessage, decode_message
from adv_helper.core.registry import ActionRegistry


@pytest.mark.parametrize("offset", [-720, -60, 0, 480, 840])
async def test_sample_and_response(offset):
    calls = []
    def sample():
        calls.append(True)
        return 1788480000123, offset
    result = await TimeModule(sample).handle(RequestMessage("request", "system.time.read", "id", {}))
    assert result.exec_id == "id"
    assert result.result.data == {"epochMilliseconds": 1788480000123, "utcOffsetMinutes": offset}
    assert len(calls) == 1


async def test_failure_isolated_by_registry():
    def broken():
        raise OSError("clock unavailable")
    registry = ActionRegistry()
    registry.register("system.time.read", TimeModule(broken).handle)
    result = await registry.dispatch(RequestMessage("request", "system.time.read", "id", {}))
    assert result.result.code == "ERROR"


def test_single_instant_and_offset_refresh(monkeypatch):
    import adv_helper.application.time_module as module
    calls = []
    offset = [480]
    class FakeDateTime:
        @staticmethod
        def fromtimestamp(value, tz):
            calls.append(value)
            class Instant:
                def astimezone(self):
                    return datetime.fromtimestamp(value, timezone(timedelta(minutes=offset[0])))
            return Instant()
    monkeypatch.setattr(module.time, "time_ns", lambda: 1788480000123456789)
    monkeypatch.setattr(module, "datetime", FakeDateTime)
    assert sample_time() == (1788480000123, 480)
    offset[0] = -60
    assert sample_time() == (1788480000123, -60)
    assert calls == [1788480000.123, 1788480000.123]


@pytest.mark.parametrize("key,value", [
    ("epochMilliseconds", True), ("epochMilliseconds", 1.5), ("epochMilliseconds", "123"),
    ("epochMilliseconds", -1), ("epochMilliseconds", 2**63),
    ("utcOffsetMinutes", True), ("utcOffsetMinutes", 841), ("utcOffsetMinutes", -721),
])
def test_invalid_time_types(key, value):
    fixture = Path(__file__).parents[2] / "protocol/fixtures/system_time_success.json"
    payload = json.loads(fixture.read_text())
    payload["result"]["data"][key] = value
    with pytest.raises(ProtocolError):
        decode_message(json.dumps(payload).encode())
