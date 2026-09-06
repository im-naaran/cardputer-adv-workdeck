import json
from pathlib import Path

import pytest

from adv_helper.core.messages import (
    ProtocolError,
    RequestMessage,
    ResponseMessage,
    decode_message,
    encode_message,
)


FIXTURES = Path(__file__).parents[2] / "protocol" / "fixtures"


@pytest.mark.parametrize("path", sorted(FIXTURES.glob("*.json")))
def test_shared_message_fixtures(path):
    if path.name == "protocol_constants.json":
        return
    if path.name in {"system_time_invalid.json", "actions_page_invalid.json"}:
        with pytest.raises(ProtocolError):
            decode_message(path.read_bytes())
        return
    message = decode_message(path.read_bytes())
    assert isinstance(message, (RequestMessage, ResponseMessage))


def test_compact_utf8_round_trip():
    request = RequestMessage("request", "codex.usage.read", "执行-1", {})
    encoded = encode_message(request)
    assert encoded.endswith(b"\n")
    assert b" " not in encoded
    assert decode_message(encoded[:-1]) == request


@pytest.mark.parametrize("payload", [b"\xff", b"not-json", json.dumps([]).encode()])
def test_invalid_messages_are_rejected(payload):
    with pytest.raises(ProtocolError):
        decode_message(payload)


def test_missing_required_field_is_rejected():
    with pytest.raises(ProtocolError, match="execId"):
        decode_message(b'{"event":"request","actionId":"x","payload":{}}')
