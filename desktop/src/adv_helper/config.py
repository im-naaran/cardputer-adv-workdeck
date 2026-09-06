from __future__ import annotations

import json
from dataclasses import dataclass, replace
from pathlib import Path
from typing import Any

from adv_helper.core.protocol_constants import ACTION_CODEX_USAGE_READ
from adv_helper.core.script_contract import valid_key, valid_name, valid_script_id, utf8_fits
from adv_helper.os_adapters.codex_app_server import DEFAULT_RPC_TIMEOUT_SECONDS


CONFIG_VERSION = 1


class ConfigError(ValueError):
    """Raised when the shared config envelope cannot be used safely."""


@dataclass(frozen=True)
class CodexSettings:
    request_timeout_seconds: int = DEFAULT_RPC_TIMEOUT_SECONDS


@dataclass(frozen=True)
class ActionConfig:
    type: str
    action_id: str
    name: str
    key: str | None
    enabled: bool
    content: str
    params: dict[str, Any]


@dataclass(frozen=True)
class AppConfig:
    config_version: int
    codex: CodexSettings
    actions: tuple[ActionConfig, ...]
    action_errors: tuple[str, ...] = ()
    warnings: tuple[str, ...] = ()
    config_directory: Path = Path(".")

    @property
    def enabled_actions(self) -> tuple[ActionConfig, ...]:
        return tuple(action for action in self.actions if action.enabled)


def load_config(path: str | Path) -> AppConfig:
    try:
        with Path(path).open("r", encoding="utf-8") as file:
            payload = json.load(file)
    except (OSError, json.JSONDecodeError) as error:
        raise ConfigError(f"cannot load config: {error}") from error
    return replace(parse_config(payload), config_directory=Path(path).resolve().parent)


def parse_config(payload: Any) -> AppConfig:
    root = _require_object(payload, "config")
    _reject_unknown(root, {"configVersion", "settings", "actions"}, "config")

    version = root.get("configVersion")
    if type(version) is not int or version != CONFIG_VERSION:
        raise ConfigError(f"configVersion must be {CONFIG_VERSION}")

    settings = _require_object(root.get("settings", {}), "settings")
    _reject_unknown(settings, {"codex"}, "settings")
    codex_payload = _require_object(settings.get("codex", {}), "settings.codex")
    _reject_unknown(
        codex_payload,
        {"refreshIntervalSeconds", "requestTimeoutSeconds"},
        "settings.codex",
    )
    warnings: list[str] = []
    if "refreshIntervalSeconds" in codex_payload:
        # Legacy input is accepted only to ease migration; ADV owns the interval.
        warnings.append("settings.codex.refreshIntervalSeconds ignored; configure ADV codex.config instead")
    request_timeout = _bounded_int(
        codex_payload.get("requestTimeoutSeconds", DEFAULT_RPC_TIMEOUT_SECONDS),
        "settings.codex.requestTimeoutSeconds",
        5,
        60,
    )

    raw_actions = root.get("actions", [])
    if not isinstance(raw_actions, list):
        raise ConfigError("actions must be an array")

    actions: list[ActionConfig] = []
    errors: list[str] = []
    seen_ids: set[str] = set()
    for index, raw_action in enumerate(raw_actions):
        try:
            action = _parse_action(raw_action, index)
            if action.action_id in seen_ids:
                raise ConfigError(f"duplicate actionId {action.action_id!r}")
            seen_ids.add(action.action_id)
            actions.append(action)
        except ConfigError as error:
            # A malformed optional action must not prevent diagnostics or other actions.
            errors.append(f"actions[{index}]: {error}")

    return AppConfig(
        config_version=version,
        codex=CodexSettings(request_timeout_seconds=request_timeout),
        actions=tuple(actions),
        action_errors=tuple(errors),
        warnings=tuple(warnings),
    )


def _parse_action(payload: Any, index: int) -> ActionConfig:
    action = _require_object(payload, f"actions[{index}]")
    fields = {"type", "actionId", "name", "key", "enabled", "content", "params"}
    _reject_unknown(action, fields, f"actions[{index}]")

    action_type = _non_empty_string(action.get("type"), "type")
    action_id = _non_empty_string(action.get("actionId"), "actionId")
    name = _non_empty_string(action.get("name"), "name")
    content = _non_empty_string(action.get("content"), "content")
    if action_type == "script":
        # Shell whitespace and newlines belong to the program, not UI metadata.
        content = action["content"]
    key = action.get("key")
    if key is not None and not isinstance(key, str):
        raise ConfigError("key must be a string or null")
    enabled = action.get("enabled", True)
    if type(enabled) is not bool:
        raise ConfigError("enabled must be a boolean")
    params = _require_object(action.get("params", {}), "params")
    if action_type == "script":
        if not valid_script_id(action_id):
            raise ConfigError("script actionId must use script. prefix and at most 64 ASCII identifier characters")
        if not valid_name(name):
            raise ConfigError("script name must be single-line text within 64 UTF-8 bytes")
        if key is not None and not key.isascii():
            raise ConfigError("script key must be one ASCII letter or null")
        key = key.lower() if key is not None else None
        if not valid_key(key):
            raise ConfigError("script key must be one ASCII letter or null")
        if "\0" in content or not utf8_fits(content, 8192):
            raise ConfigError("script content contains NUL, invalid UTF-8 or exceeds 8192 bytes")
        _reject_unknown(params, {"timeoutSeconds"}, "script.params")
        params = {"timeoutSeconds": _bounded_int(params.get("timeoutSeconds", 15), "timeoutSeconds", 1, 30)}
    elif action_type != "codex" or action_id != ACTION_CODEX_USAGE_READ or content != "usage":
        raise ConfigError("unsupported action; expected codex.usage.read or script")
    return ActionConfig(action_type, action_id, name, key, enabled, content, params)


def _require_object(value: Any, field: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise ConfigError(f"{field} must be an object")
    return value


def _reject_unknown(value: dict[str, Any], allowed: set[str], field: str) -> None:
    unknown = sorted(set(value) - allowed)
    if unknown:
        raise ConfigError(f"{field} contains unknown fields: {', '.join(unknown)}")


def _bounded_int(value: Any, field: str, minimum: int, maximum: int) -> int:
    if type(value) is not int or not minimum <= value <= maximum:
        raise ConfigError(f"{field} must be an integer between {minimum} and {maximum}")
    return value


def _non_empty_string(value: Any, field: str) -> str:
    if not isinstance(value, str) or not value.strip():
        raise ConfigError(f"{field} must be a non-empty string")
    return value.strip()
