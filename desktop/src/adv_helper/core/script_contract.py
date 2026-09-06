"""Shared script metadata validation; limits bound a page, never the catalog."""
from __future__ import annotations

import re
from typing import Any

from .protocol_constants import SCRIPT_PAGE_SIZE


def valid_script_id(value: Any) -> bool:
    return isinstance(value, str) and re.fullmatch(r"script\.[A-Za-z0-9_.-]+", value) is not None and len(value) <= 64


def utf8_fits(value: str, limit: int) -> bool:
    try:
        return len(value.encode("utf-8")) <= limit
    except UnicodeEncodeError:
        # JSON can contain unpaired surrogate escapes; isolate the invalid entry.
        return False


def valid_name(value: Any) -> bool:
    return (isinstance(value, str) and bool(value.strip())
            and utf8_fits(value, 64)
            and all(ord(c) >= 32 and ord(c) != 127 for c in value))


def valid_key(value: Any) -> bool:
    return value is None or (isinstance(value, str) and len(value) == 1 and "a" <= value <= "z")


def unsigned(value: Any) -> bool:
    return type(value) is int and 0 <= value <= 2**64 - 1


def validate_page(data: Any) -> None:
    if not isinstance(data, dict):
        raise ValueError("invalid actions page")
    offset, total, next_offset, items = (data.get(k) for k in ("offset", "total", "nextOffset", "actions"))
    if (not unsigned(offset) or not unsigned(total) or offset % SCRIPT_PAGE_SIZE
            or "nextOffset" not in data or not isinstance(items, list)
            or len(items) > SCRIPT_PAGE_SIZE or (total > 0 and offset >= total)
            or (total == 0 and offset != 0)):
        raise ValueError("invalid pagination")
    count = min(SCRIPT_PAGE_SIZE, total - offset)
    expected_next = offset + count if offset + count < total else None
    if len(items) != count or next_offset != expected_next or (next_offset is not None and not unsigned(next_offset)):
        raise ValueError("inconsistent pagination")
    seen: set[str] = set()
    for item in items:
        if (not isinstance(item, dict) or item.get("type") != "script"
                or not valid_script_id(item.get("actionId")) or not valid_name(item.get("name"))
                or "key" not in item or not valid_key(item["key"])
                or "effectiveKey" not in item or not valid_key(item["effectiveKey"])
                or (item["effectiveKey"] is not None and item["effectiveKey"] != item["key"])
                or item["actionId"] in seen):
            raise ValueError("invalid script metadata")
        seen.add(item["actionId"])


def validate_execution(code: str, data: Any) -> None:
    # ERROR/BUSY before resolution have no actual script to report.
    if data is None and code != "OK":
        return
    if not isinstance(data, dict) or not valid_script_id(data.get("actionId")) or not valid_name(data.get("name")):
        raise ValueError("invalid execution identity")
    exit_code = data.get("exitCode")
    if exit_code is not None and (type(exit_code) is not int or not -(2**31) <= exit_code < 2**31):
        raise ValueError("invalid exit code")
    if code == "OK" and (type(exit_code) is not int or exit_code != 0):
        raise ValueError("successful execution requires exitCode zero")
