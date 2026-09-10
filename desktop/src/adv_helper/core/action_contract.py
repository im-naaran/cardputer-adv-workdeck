"""Typed action contracts for protocol v2; no configuration text travels here."""
from __future__ import annotations

import re
from typing import Any

from .protocol_constants import ACTION_ACTIONS_LIST, ACTION_PAGE_SIZE, ACTION_EXECUTE, ACTION_SHORTCUT_EXECUTE

ACTION_TYPES = ("script", "clipboard")
FAILURE_REASONS = frozenset({"UNAVAILABLE", "PERMISSION_DENIED", "WRITE_FAILED",
                             "PASTE_FAILED", "UNCONFIRMED", "EXECUTION_FAILED"})


def valid_action_id(value: Any, action_type: str) -> bool:
    return (action_type in ACTION_TYPES and isinstance(value, str) and len(value) <= 64
            and re.fullmatch(re.escape(action_type) + r"\.[A-Za-z0-9_.-]+", value) is not None)


def utf8_fits(value: str, limit: int) -> bool:
    try:
        return len(value.encode("utf-8")) <= limit
    except UnicodeEncodeError:
        # JSON permits escaped unpaired surrogates; reject the optional item alone.
        return False


def valid_name(value: Any) -> bool:
    return (isinstance(value, str) and bool(value.strip()) and utf8_fits(value, 64)
            and all(ord(c) >= 32 and ord(c) != 127 for c in value))


def valid_key(value: Any) -> bool:
    return value is None or (isinstance(value, str) and len(value) == 1 and "a" <= value <= "z")


def unsigned(value: Any) -> bool:
    return type(value) is int and 0 <= value <= 2**64 - 1


def validate_supported_types(value: Any) -> None:
    if (not isinstance(value, list) or any(t not in ACTION_TYPES for t in value)
            or len(set(value)) != len(value)):
        raise ValueError("invalid supported action types")


def validate_request(action_id: str, payload: Any) -> None:
    if not isinstance(payload, dict):
        raise ValueError("invalid action payload")
    if action_id == ACTION_ACTIONS_LIST:
        valid = (set(payload) == {"type", "offset"} and payload["type"] in ACTION_TYPES
                 and unsigned(payload["offset"]) and payload["offset"] % ACTION_PAGE_SIZE == 0)
    elif action_id == ACTION_EXECUTE:
        valid = (set(payload) == {"actionId"}
                 and any(valid_action_id(payload["actionId"], t) for t in ACTION_TYPES))
    elif action_id == ACTION_SHORTCUT_EXECUTE:
        valid = set(payload) == {"key"} and payload["key"] is not None and valid_key(payload["key"])
    else:
        valid = False
    if not valid:
        raise ValueError("invalid action request")


def validate_page(data: Any) -> None:
    if not isinstance(data, dict) or data.get("type") not in ACTION_TYPES:
        raise ValueError("invalid actions page type")
    offset, total, next_offset, items = (data.get(k) for k in ("offset", "total", "nextOffset", "actions"))
    if (not unsigned(offset) or not unsigned(total) or offset % ACTION_PAGE_SIZE
            or "nextOffset" not in data or not isinstance(items, list)
            or (offset >= total if total else offset != 0)):
        raise ValueError("invalid pagination")
    count = min(ACTION_PAGE_SIZE, total - offset)
    expected_next = offset + count if offset + count < total else None
    if (len(items) != count or next_offset != expected_next
            or (next_offset is not None and not unsigned(next_offset))):
        raise ValueError("inconsistent pagination")
    seen: set[str] = set()
    for item in items:
        if (not isinstance(item, dict) or item.get("type") != data["type"]
                or not valid_action_id(item.get("actionId"), data["type"])
                or not valid_name(item.get("name")) or "key" not in item or not valid_key(item["key"])
                or "effectiveKey" not in item or not valid_key(item["effectiveKey"])
                or (item["effectiveKey"] is not None and item["effectiveKey"] != item["key"])
                or item["actionId"] in seen
                or any(field in item for field in ("content", "params", "enabled"))):
            raise ValueError("invalid action metadata")
        seen.add(item["actionId"])


def validate_execution(code: str, data: Any) -> None:
    if code not in ("OK", "ERROR", "BUSY", "TIMEOUT"):
        raise ValueError("invalid action result code")
    # Pre-resolution failures have no identity; resolved results carry all nullable fields.
    if data is None and code != "OK":
        return
    fields = {"type", "actionId", "name", "exitCode", "clipboardWritten", "pasteSent", "reason"}
    if (not isinstance(data, dict) or not fields <= data.keys()
            or not valid_action_id(data.get("actionId"), data.get("type"))
            or not valid_name(data.get("name"))
            or any(field in data for field in ("content", "params", "enabled"))):
        raise ValueError("invalid execution identity")
    reason = data["reason"]
    if reason is not None and (not isinstance(reason, str) or reason not in FAILURE_REASONS):
        raise ValueError("invalid execution reason")
    if code == "OK" and reason is not None:
        raise ValueError("successful action cannot have failure reason")
    exit_code, written, sent = (data[k] for k in ("exitCode", "clipboardWritten", "pasteSent"))
    if data["type"] == "script":
        if (written is not None or sent is not None or
                (exit_code is not None and (type(exit_code) is not int or not -(2**31) <= exit_code < 2**31))
                or (code == "OK" and (type(exit_code) is not int or exit_code != 0))):
            raise ValueError("invalid script execution result")
    else:
        # None means an invoked OS operation did not yield a confirmed outcome, not false.
        if (exit_code is not None or any(v is not None and type(v) is not bool for v in (written, sent))
                or (sent is True and written is not True)
                or (code == "OK" and (written is not True or sent is not True))):
            raise ValueError("invalid clipboard execution result")
