from __future__ import annotations

import json
from dataclasses import dataclass
from typing import Any, Literal

from .script_contract import validate_page, validate_execution
from .protocol_constants import (
    ACTION_ACTIONS_LIST, ACTION_SCRIPTS_EXECUTE, ACTION_SHORTCUT_EXECUTE,
    ACTION_CODEX_USAGE_READ,
    ACTION_SYSTEM_HELLO,
    ACTION_SYSTEM_TIME_READ,
    MAX_JSON_BYTES,
    RESULT_CODES,
)


class ProtocolError(ValueError):
    """An ADV protocol message is malformed or exceeds transport limits."""


@dataclass(frozen=True)
class RequestMessage:
    event: Literal["request"]
    action_id: str
    exec_id: str
    payload: dict[str, Any]

    def to_dict(self) -> dict[str, Any]:
        return {
            "event": self.event,
            "actionId": self.action_id,
            "execId": self.exec_id,
            "payload": self.payload,
        }


@dataclass(frozen=True)
class Result:
    code: str
    msg: str
    data: Any


@dataclass(frozen=True)
class ResponseMessage:
    event: Literal["response"]
    action_id: str
    exec_id: str
    result: Result

    def to_dict(self) -> dict[str, Any]:
        return {
            "event": self.event,
            "actionId": self.action_id,
            "execId": self.exec_id,
            "result": {
                "code": self.result.code,
                "msg": self.result.msg,
                "data": self.result.data,
            },
        }


Message = RequestMessage | ResponseMessage


def parse_message(payload: Any) -> Message:
    if not isinstance(payload, dict):
        raise ProtocolError("message must be an object")
    event = payload.get("event")
    action_id = _required_string(payload.get("actionId"), "actionId")
    exec_id = _required_string(payload.get("execId"), "execId")

    if event == "request":
        body = payload.get("payload")
        if not isinstance(body, dict):
            raise ProtocolError("request payload must be an object")
        return RequestMessage("request", action_id, exec_id, body)
    if event == "response":
        raw_result = payload.get("result")
        if not isinstance(raw_result, dict):
            raise ProtocolError("response result must be an object")
        code = _required_string(raw_result.get("code"), "result.code")
        if code not in RESULT_CODES:
            raise ProtocolError(f"unknown result.code {code!r}")
        msg = raw_result.get("msg")
        if not isinstance(msg, str):
            raise ProtocolError("result.msg must be a string")
        if "data" not in raw_result:
            raise ProtocolError("response result requires data")
        if action_id == ACTION_SYSTEM_TIME_READ and code == "OK":
            data = raw_result["data"]
            if not isinstance(data, dict):
                raise ProtocolError("time data must be an object")
            epoch_ms, offset = data.get("epochMilliseconds"), data.get("utcOffsetMinutes")
            if (type(epoch_ms) is not int or not 0 <= epoch_ms <= 2**63 - 1
                    or type(offset) is not int or not -720 <= offset <= 840):
                raise ProtocolError("invalid time data")
        try:
            if action_id == ACTION_ACTIONS_LIST and code == "OK":
                validate_page(raw_result["data"])
            elif action_id in (ACTION_SCRIPTS_EXECUTE, ACTION_SHORTCUT_EXECUTE):
                validate_execution(code, raw_result["data"])
        except ValueError as error:
            raise ProtocolError(str(error)) from error
        return ResponseMessage("response", action_id, exec_id, Result(code, msg, raw_result["data"]))
    raise ProtocolError("event must be request or response")


def decode_message(line: bytes) -> Message:
    if len(line) > MAX_JSON_BYTES:
        raise ProtocolError(f"message exceeds {MAX_JSON_BYTES} bytes")
    try:
        text = line.decode("utf-8")
    except UnicodeDecodeError as error:
        raise ProtocolError("message is not valid UTF-8") from error
    try:
        payload = json.loads(text)
    except json.JSONDecodeError as error:
        raise ProtocolError("message is not valid JSON") from error
    return parse_message(payload)


def encode_message(message: Message) -> bytes:
    line = json.dumps(message.to_dict(), ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(line) > MAX_JSON_BYTES:
        raise ProtocolError(f"message exceeds {MAX_JSON_BYTES} bytes")
    return line + b"\n"


def error_response(request: RequestMessage, code: str, message: str) -> ResponseMessage:
    if code not in RESULT_CODES:
        raise ProtocolError(f"unknown result code {code!r}")
    return ResponseMessage("response", request.action_id, request.exec_id, Result(code, message, None))


def _required_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ProtocolError(f"{field} must be a non-empty string")
    if len(value) > 128:
        raise ProtocolError(f"{field} is too long")
    return value
