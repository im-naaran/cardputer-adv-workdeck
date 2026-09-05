import pytest

from adv_helper.core.framing import FrameTimeoutError, FrameTooLargeError, JsonlBuffer


def test_split_utf8_and_sticky_packets():
    buffer = JsonlBuffer()
    payload = '{"text":"中文"}\n{"next":1}\n'.encode()
    split = payload.index("中".encode()) + 1
    assert buffer.feed(payload[:split]) == []
    assert buffer.feed(payload[split:]) == [
        '{"text":"中文"}'.encode(),
        b'{"next":1}',
    ]


def test_oversize_clears_buffer():
    buffer = JsonlBuffer(max_json_bytes=4)
    with pytest.raises(FrameTooLargeError):
        buffer.feed(b"12345")
    assert buffer.buffered_bytes == 0


def test_incomplete_line_timeout_clears_buffer():
    now = [0.0]
    buffer = JsonlBuffer(timeout_seconds=2, clock=lambda: now[0])
    buffer.feed(b"{")
    now[0] = 2.0
    with pytest.raises(FrameTimeoutError):
        buffer.check_timeout()
    assert buffer.buffered_bytes == 0
