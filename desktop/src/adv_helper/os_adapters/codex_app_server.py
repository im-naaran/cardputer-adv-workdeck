from __future__ import annotations

import asyncio
import json
import logging
import time
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Any, Protocol


class CodexProviderError(RuntimeError):
    pass


class NotLoggedInError(CodexProviderError):
    pass


class CodexUnavailableError(CodexProviderError):
    pass


class CodexTimeoutError(CodexProviderError):
    pass


class InvalidCodexResponseError(CodexProviderError):
    pass


@dataclass(frozen=True)
class UsageWindow:
    limit_id: str
    limit_name: str | None
    window_kind: str
    used_percent: int
    window_duration_mins: int | None
    resets_at_epoch_seconds: int | None

    def to_protocol_dict(self) -> dict[str, Any]:
        return {
            "limitId": self.limit_id,
            "limitName": self.limit_name,
            "windowKind": self.window_kind,
            "usedPercent": self.used_percent,
            "windowDurationMins": self.window_duration_mins,
            "resetsAtEpochSeconds": self.resets_at_epoch_seconds,
        }


@dataclass(frozen=True)
class UsageSnapshot:
    fetched_at_epoch_seconds: int
    windows: tuple[UsageWindow, ...]

    def to_protocol_dict(self) -> dict[str, Any]:
        return {
            "fetchedAtEpochSeconds": self.fetched_at_epoch_seconds,
            "windows": [window.to_protocol_dict() for window in self.windows],
        }


class RateLimitProvider(Protocol):
    async def read_usage(self) -> UsageSnapshot: ...


class ProcessLike(Protocol):
    stdin: Any
    stdout: Any
    returncode: int | None

    def terminate(self) -> None: ...

    async def wait(self) -> int: ...


ProcessFactory = Callable[[], Awaitable[ProcessLike]]
_MISSING = object()
_LOGGER = logging.getLogger("adv_helper.codex_app_server")


async def _default_process_factory() -> ProcessLike:
    return await asyncio.create_subprocess_exec(
        "codex",
        "app-server",
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        # App Server diagnostics may contain account context, so do not mirror raw stderr.
        stderr=asyncio.subprocess.DEVNULL,
    )


DEFAULT_RPC_TIMEOUT_SECONDS = 15


class CodexAppServerClient:
    def __init__(
        self,
        *,
        timeout_seconds: float = DEFAULT_RPC_TIMEOUT_SECONDS,
        process_factory: ProcessFactory = _default_process_factory,
        epoch_clock: Callable[[], float] = time.time,
    ) -> None:
        self._timeout_seconds = timeout_seconds
        self._process_factory = process_factory
        self._epoch_clock = epoch_clock
        self._process: ProcessLike | None = None
        self._initialized = False
        self._next_id = 1
        self._request_lock = asyncio.Lock()

    async def read_usage(self) -> UsageSnapshot:
        # Start lazily so an idle desktop process never touches Codex without an ADV request.
        async with self._request_lock:
            started_at = time.monotonic()
            _LOGGER.info("Codex usage query started")
            await self._ensure_initialized()
            account_result = await self._rpc("account/read", {"refreshToken": False})
            account = account_result.get("account") if isinstance(account_result, dict) else None
            if not isinstance(account, dict) or account.get("type") not in {
                "chatgpt",
                "chatgptAuthTokens",
                "agentIdentity",
                "personalAccessToken",
            }:
                raise NotLoggedInError("Codex ChatGPT login is not available")
            rate_limits = await self._rpc("account/rateLimits/read")
            snapshot = normalize_rate_limits(rate_limits, int(self._epoch_clock()))
            _LOGGER.info(
                "Codex usage query completed windows=%d durationMs=%d",
                len(snapshot.windows),
                round((time.monotonic() - started_at) * 1000),
            )
            return snapshot

    async def _ensure_initialized(self) -> None:
        if self._process is not None and self._process.returncode is None and self._initialized:
            return
        self._forget_process()
        try:
            self._process = await self._process_factory()
        except (FileNotFoundError, OSError) as error:
            raise CodexUnavailableError("cannot start Codex App Server") from error
        self._next_id = 1
        _LOGGER.info("Codex App Server process started")
        if self._process.stdin is None or self._process.stdout is None:
            self._forget_process()
            raise CodexUnavailableError("Codex App Server stdio is unavailable")
        try:
            await self._rpc(
                "initialize",
                {
                    "clientInfo": {
                        "name": "cardputer_adv_workdeck",
                        "title": "Cardputer ADV Workdeck",
                        "version": "0.1.0",
                    }
                },
            )
            await self._notify("initialized", {})
        except Exception:
            await self._abandon_process(timeout_seconds=0.2)
            raise
        self._initialized = True

    async def _rpc(self, method: str, params: Any = _MISSING) -> dict[str, Any]:
        process = self._require_process()
        request_id = self._next_id
        self._next_id += 1
        request: dict[str, Any] = {"method": method, "id": request_id}
        if params is not _MISSING:
            request["params"] = params
        await self._write_line(request)
        started_at = time.monotonic()
        _LOGGER.info("Codex RPC started method=%s requestId=%d", method, request_id)

        try:
            async with asyncio.timeout(self._timeout_seconds):
                while True:
                    line = await process.stdout.readline()
                    if not line:
                        self._forget_process()
                        raise CodexUnavailableError("Codex App Server exited")
                    try:
                        response = json.loads(line)
                    except (UnicodeDecodeError, json.JSONDecodeError) as error:
                        raise InvalidCodexResponseError("Codex App Server returned invalid JSON") from error
                    if not isinstance(response, dict):
                        raise InvalidCodexResponseError("Codex App Server response must be an object")
                    # Notifications and unrelated IDs are not responses to the active RPC.
                    if response.get("id") != request_id:
                        continue
                    if "error" in response:
                        raise InvalidCodexResponseError(f"Codex App Server rejected {method}")
                    result = response.get("result")
                    if not isinstance(result, dict):
                        raise InvalidCodexResponseError("Codex App Server result must be an object")
                    _LOGGER.info(
                        "Codex RPC completed method=%s requestId=%d durationMs=%d",
                        method,
                        request_id,
                        round((time.monotonic() - started_at) * 1000),
                    )
                    return result
        except TimeoutError as error:
            _LOGGER.warning(
                "Codex RPC timed out method=%s requestId=%d timeoutSeconds=%s",
                method,
                request_id,
                self._timeout_seconds,
            )
            raise CodexTimeoutError(f"Codex App Server timed out during {method}") from error

    async def _notify(self, method: str, params: dict[str, Any]) -> None:
        self._require_process()
        await self._write_line({"method": method, "params": params})

    async def _write_line(self, payload: dict[str, Any]) -> None:
        process = self._require_process()
        line = json.dumps(payload, separators=(",", ":")).encode() + b"\n"
        try:
            process.stdin.write(line)
            drain = getattr(process.stdin, "drain", None)
            if callable(drain):
                await drain()
        except (BrokenPipeError, ConnectionError, OSError) as error:
            self._forget_process()
            raise CodexUnavailableError("cannot write to Codex App Server") from error

    def _require_process(self) -> ProcessLike:
        if self._process is None or self._process.returncode is not None:
            raise CodexUnavailableError("Codex App Server is not running")
        return self._process

    def _forget_process(self) -> None:
        self._process = None
        self._initialized = False

    async def _abandon_process(self, *, timeout_seconds: float) -> None:
        process = self._process
        self._forget_process()
        if process is not None and process.returncode is None:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), timeout_seconds)
            except TimeoutError:
                pass

    async def close(self) -> None:
        await self._abandon_process(timeout_seconds=2)


def normalize_rate_limits(payload: Any, fetched_at: int) -> UsageSnapshot:
    if not isinstance(payload, dict):
        raise InvalidCodexResponseError("rate limits result must be an object")

    buckets: list[tuple[str | None, Any]]
    multi = payload.get("rateLimitsByLimitId")
    if multi is not None:
        if not isinstance(multi, dict):
            raise InvalidCodexResponseError("rateLimitsByLimitId must be an object")
        buckets = [(key, multi[key]) for key in sorted(multi)]
    elif "rateLimits" in payload:
        buckets = [(None, payload["rateLimits"])]
    else:
        raise InvalidCodexResponseError("rate limits are missing")

    windows: list[UsageWindow] = []
    for fallback_id, raw_bucket in buckets:
        if not isinstance(raw_bucket, dict):
            raise InvalidCodexResponseError("rate limit bucket must be an object")
        limit_id = raw_bucket.get("limitId") or fallback_id or "codex"
        if not isinstance(limit_id, str) or not limit_id:
            raise InvalidCodexResponseError("rate limit bucket requires limitId")
        limit_name = raw_bucket.get("limitName")
        if limit_name is not None and not isinstance(limit_name, str):
            raise InvalidCodexResponseError("limitName must be a string or null")
        for kind in ("primary", "secondary"):
            raw_window = raw_bucket.get(kind)
            if raw_window is None:
                continue
            if not isinstance(raw_window, dict):
                raise InvalidCodexResponseError(f"{kind} window must be an object or null")
            used_percent = raw_window.get("usedPercent")
            if type(used_percent) is not int:
                raise InvalidCodexResponseError("usedPercent must be an integer")
            duration = _optional_int(raw_window.get("windowDurationMins"), "windowDurationMins")
            resets_at = _optional_int(raw_window.get("resetsAt"), "resetsAt")
            windows.append(UsageWindow(limit_id, limit_name, kind, used_percent, duration, resets_at))
    return UsageSnapshot(fetched_at, tuple(windows))


def _optional_int(value: Any, field: str) -> int | None:
    if value is None:
        return None
    if type(value) is not int:
        raise InvalidCodexResponseError(f"{field} must be an integer or null")
    return value
