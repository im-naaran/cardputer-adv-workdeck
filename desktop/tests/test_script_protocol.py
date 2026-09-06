import copy
import json
from pathlib import Path

import pytest

from adv_helper.core.messages import ProtocolError, decode_message, encode_message, parse_message


FIXTURES = Path(__file__).parents[2] / "protocol/fixtures"


def fixture(name):
    return json.loads((FIXTURES / name).read_text())


@pytest.mark.parametrize("name", [
    "actions_page_first.json", "actions_page_last.json", "actions_page_empty.json",
    "script_execute_success.json", "script_execute_error.json", "script_shortcut_success.json",
    "actions_list_request.json", "script_execute_request.json", "script_shortcut_request.json",
])
def test_script_fixtures_roundtrip(name):
    message = parse_message(fixture(name))
    assert decode_message(encode_message(message)[:-1]) == message


@pytest.mark.parametrize("field,value", [
    ("offset", True), ("offset", -1), ("offset", 1), ("total", 1.5),
    ("total", 2**64), ("nextOffset", False), ("nextOffset", 16), ("actions", []),
])
def test_bad_pagination(field, value):
    data = fixture("actions_page_first.json")
    data["result"]["data"][field] = value
    with pytest.raises(ProtocolError):
        parse_message(data)


@pytest.mark.parametrize("field,value", [
    ("type", "clipboard"), ("actionId", "system.time.read"), ("name", "\n"),
    ("key", "G"), ("effectiveKey", "b"),
])
def test_bad_item(field, value):
    data = fixture("actions_page_first.json")
    data["result"]["data"]["actions"][0][field] = value
    with pytest.raises(ProtocolError):
        parse_message(data)


def test_missing_null_fields_duplicate_id_and_future_fields():
    source = fixture("actions_page_first.json")
    for field in ("nextOffset", "key", "effectiveKey"):
        data = copy.deepcopy(source)
        parent = data["result"]["data"]
        if field != "nextOffset":
            parent = parent["actions"][0]
        del parent[field]
        with pytest.raises(ProtocolError):
            parse_message(data)
    data = copy.deepcopy(source)
    data["result"]["data"]["actions"][1]["actionId"] = "script.test.0"
    with pytest.raises(ProtocolError):
        parse_message(data)
    source["result"]["data"]["future"] = True
    assert parse_message(source)
    with pytest.raises(ProtocolError):
        parse_message(fixture("actions_page_invalid.json"))


@pytest.mark.parametrize("exit_code", [None, True, 1, "0", 0.5, 2**32])
def test_success_requires_integer_zero(exit_code):
    data = fixture("script_execute_success.json")
    data["result"]["data"]["exitCode"] = exit_code
    with pytest.raises(ProtocolError):
        parse_message(data)


def test_maximum_escaped_page_fits_frame():
    data = fixture("actions_page_first.json")
    data["execId"] = '"' * 128
    for i, item in enumerate(data["result"]["data"]["actions"]):
        item["name"] = '\\"' * 32
        item["actionId"] = "script." + str(i) + "x" * 56
    assert len(encode_message(parse_message(data))) - 1 <= 4096
