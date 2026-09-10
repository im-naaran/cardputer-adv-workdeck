from __future__ import annotations

import asyncio
import secrets
import time
from collections.abc import Callable
from typing import Any, Protocol

from adv_helper.application.codex_module import CodexModule
from adv_helper.application.time_module import TimeModule
from adv_helper.application.actions_module import ActionsModule
from adv_helper.os_adapters.paste_runner import PasteRunner
from adv_helper.os_adapters.script_runner import ScriptRunner
from adv_helper.config import AppConfig
from adv_helper.core.framing import FrameError, JsonlBuffer
from adv_helper.core.messages import (
    ACTION_CODEX_USAGE_READ,
    ACTION_SYSTEM_HELLO,
    ProtocolError,
    RequestMessage,
    ResponseMessage,
    Result,
    decode_message,
    encode_message,
)
from adv_helper.core.protocol_constants import (ACTION_SYSTEM_TIME_READ, PROTOCOL_VERSION,
    ACTION_ACTIONS_LIST, ACTION_EXECUTE, ACTION_SHORTCUT_EXECUTE)
from adv_helper.core.registry import ActionRegistry
from adv_helper.core.session import ConnectionSession
from adv_helper.os_adapters.codex_app_server import CodexAppServerClient, RateLimitProvider
from adv_helper.os_adapters.computer_identity import ComputerIdentity, load_computer_identity
from adv_helper.platform.ble_transport import BleTransport, BleTransportConfig
from adv_helper.platform.diagnostics import LocalDiagnostics


class SessionTransport(Protocol):
    async def send(self, payload: bytes) -> None: ...

    async def receive_chunk(self) -> bytes: ...


class ModuleManager:
    def __init__(self, registry: ActionRegistry, diagnostics: LocalDiagnostics) -> None:
        self._registry = registry
        self._diagnostics = diagnostics

    def enable(self, action_id: str, factory: Callable[[], Any]) -> None:
        self.enable_group((action_id,), factory)

    def enable_group(self, action_ids: tuple[str, ...], factory: Callable[[], Any]) -> Any:
        try:
            module = factory()
            for action_id in action_ids:
                self._registry.register(action_id, module.handle)
            return module
        except Exception as error:
            # Module startup failure is isolated so BLE and local diagnostics stay available.
            self._diagnostics.error("module initialization failed", actionIds=action_ids, errorType=type(error).__name__)


class DesktopApplication:
    def __init__(
        self,
        config: AppConfig,
        registry: ActionRegistry,
        diagnostics: LocalDiagnostics,
        *,
        provider: RateLimitProvider,
        computer_identity: ComputerIdentity,
        receive_poll_seconds: float = 1,
        frame_timeout_seconds: float = 5,
        actions: ActionsModule | None = None,
    ) -> None:
        self.actions = actions
        self.config = config
        self.registry = registry
        self.diagnostics = diagnostics
        self.provider = provider
        self.computer_identity = computer_identity
        self.receive_poll_seconds = receive_poll_seconds
        self.frame_timeout_seconds = frame_timeout_seconds
        self.connection_session = ConnectionSession()
        self._send_lock = asyncio.Lock()

    async def run_session(self, transport: SessionTransport) -> None:
        self.connection_session.connect()
        buffer = JsonlBuffer(timeout_seconds=self.frame_timeout_seconds)
        request_tasks: set[asyncio.Task[None]] = set()
        try:
            # Hello initializes the BLE session; Codex usage remains request-driven by ADV.
            await self._send_message(transport, self._hello_message())
            self.diagnostics.info("ADV session ready", capabilities=list(self.registry.capabilities))
            while True:
                try:
                    chunk = await asyncio.wait_for(
                        transport.receive_chunk(),
                        timeout=self.receive_poll_seconds,
                    )
                except TimeoutError:
                    try:
                        buffer.check_timeout()
                    except FrameError as error:
                        self.diagnostics.warning("discarded timed-out BLE frame", errorType=type(error).__name__)
                    continue
                try:
                    lines = buffer.feed(chunk)
                except FrameError as error:
                    self.diagnostics.warning("discarded invalid BLE frame", errorType=type(error).__name__)
                    continue
                for line in lines:
                    try:
                        message = decode_message(line)
                    except ProtocolError as error:
                        self.diagnostics.warning("discarded invalid protocol message", errorType=type(error).__name__)
                        continue
                    if not isinstance(message, RequestMessage):
                        self.diagnostics.debug("ignored non-request ADV message", actionId=message.action_id)
                        continue
                    task = asyncio.create_task(self._handle_request(transport, message))
                    request_tasks.add(task)
                    task.add_done_callback(request_tasks.discard)
                    task.add_done_callback(self._observe_request_task)
        finally:
            for task in request_tasks:
                task.cancel()
            await asyncio.gather(*request_tasks, return_exceptions=True)
            self.connection_session.disconnect()
            self.diagnostics.info("ADV session ended")

    async def _handle_request(self, transport: SessionTransport, message: RequestMessage) -> None:
        started_at = time.monotonic()
        self.diagnostics.info(
            "ADV request received",
            actionId=message.action_id,
            execId=message.exec_id,
        )
        owns_request = self.connection_session.begin_request(message.exec_id)
        try:
            if not owns_request:
                response = ResponseMessage(
                    "response", message.action_id, message.exec_id,
                    Result("BUSY", "request already active", None),
                )
            else:
                response = await self.registry.dispatch(message)
            await self._send_message(transport, response)
        finally:
            # The request remains active while its response waits for the send lock
            # or BLE. A duplicate must not re-execute a completed side effect.
            if owns_request:
                self.connection_session.complete_request(message.exec_id)
        self.diagnostics.info(
            "ADV response sent",
            actionId=response.action_id,
            execId=response.exec_id,
            resultCode=response.result.code,
            durationMs=round((time.monotonic() - started_at) * 1000),
        )

    async def _send_message(self, transport: SessionTransport, message: ResponseMessage) -> None:
        # Keep all chunks for one JSONL message contiguous when handlers finish together.
        async with self._send_lock:
            await transport.send(encode_message(message))

    def _observe_request_task(self, task: asyncio.Task[None]) -> None:
        if task.cancelled():
            return
        error = task.exception()
        if error is not None:
            self.diagnostics.error("request response failed", errorType=type(error).__name__)

    def _hello_message(self) -> ResponseMessage:
        return ResponseMessage(
            "response",
            ACTION_SYSTEM_HELLO,
            secrets.token_hex(8),
            Result(
                "OK",
                "session ready",
                {
                    "protocolVersion": PROTOCOL_VERSION,
                    "computerId": self.computer_identity.stable_id,
                    "computerName": self.computer_identity.display_name,
                    "capabilities": list(self.registry.capabilities),
                    "supportedActionTypes": list(self.actions.supported_action_types) if self.actions else [],
                },
            ),
        )

    async def close(self) -> None:
        close = getattr(self.provider, "close", None)
        if close is not None:
            await close()


def build_application(
    config: AppConfig,
    diagnostics: LocalDiagnostics,
    *,
    provider: RateLimitProvider | None = None,
    script_runner: ScriptRunner | None = None,
    paste_runner: PasteRunner | None = None,
    computer_identity: ComputerIdentity | None = None,
    receive_poll_seconds: float = 1,
    frame_timeout_seconds: float = 5,
) -> DesktopApplication:
    registry = ActionRegistry()
    registry.register(ACTION_SYSTEM_TIME_READ, TimeModule().handle)
    actual_provider = provider or CodexAppServerClient(timeout_seconds=config.codex.request_timeout_seconds)
    modules = ModuleManager(registry, diagnostics)
    actions = modules.enable_group((ACTION_ACTIONS_LIST, ACTION_EXECUTE, ACTION_SHORTCUT_EXECUTE),
                                   lambda: ActionsModule(config, script_runner, diagnostics, paste_runner=paste_runner))
    enabled_ids = {action.action_id for action in config.enabled_actions}
    if ACTION_CODEX_USAGE_READ in enabled_ids:
        modules.enable(ACTION_CODEX_USAGE_READ, lambda: CodexModule(actual_provider))
    for error in config.action_errors:
        diagnostics.warning("ignored invalid action config", detail=error)
    for warning in config.warnings:
        diagnostics.warning("configuration default applied", detail=warning)
    return DesktopApplication(
        config,
        registry,
        diagnostics,
        actions=actions,
        provider=actual_provider,
        computer_identity=computer_identity or load_computer_identity(),
        receive_poll_seconds=receive_poll_seconds,
        frame_timeout_seconds=frame_timeout_seconds,
    )


def build_ble_transport(
    *,
    device_name: str = "",
    device_id: str = "",
    on_error: Callable[[str], None] | None = None,
) -> BleTransport:
    return BleTransport(
        BleTransportConfig(device_name=device_name, device_id=device_id),
        on_error=on_error,
    )
