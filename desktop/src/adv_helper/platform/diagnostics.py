from __future__ import annotations

import json
import logging
from typing import Any


SENSITIVE_KEY_PARTS = ("token", "auth", "credential", "password", "secret")


class LocalDiagnostics:
    def __init__(self, logger: logging.Logger | None = None, *, verbose: bool = False) -> None:
        self._logger = logger or logging.getLogger("adv_helper")
        self._verbose = verbose

    def info(self, message: str, **fields: Any) -> None:
        self._write(logging.INFO, message, fields)

    def warning(self, message: str, **fields: Any) -> None:
        self._write(logging.WARNING, message, fields)

    def error(self, message: str, **fields: Any) -> None:
        self._write(logging.ERROR, message, fields)

    def debug(self, message: str, **fields: Any) -> None:
        if self._verbose:
            self._write(logging.DEBUG, message, fields)

    def _write(self, level: int, message: str, fields: dict[str, Any]) -> None:
        # Diagnostics log allowlisted summaries; any accidental auth-shaped field is redacted.
        safe_fields = _redact(fields)
        suffix = f" {json.dumps(safe_fields, ensure_ascii=False, sort_keys=True)}" if safe_fields else ""
        self._logger.log(level, "%s%s", message, suffix)


def configure_logging(verbose: bool = False) -> LocalDiagnostics:
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.INFO,
        format="%(asctime)s %(levelname)s %(message)s",
    )
    return LocalDiagnostics(verbose=verbose)


def _redact(value: Any, key: str = "") -> Any:
    if any(part in key.lower() for part in SENSITIVE_KEY_PARTS):
        return "[REDACTED]"
    if isinstance(value, dict):
        return {str(item_key): _redact(item_value, str(item_key)) for item_key, item_value in value.items()}
    if isinstance(value, (list, tuple)):
        return [_redact(item) for item in value]
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return repr(value)
