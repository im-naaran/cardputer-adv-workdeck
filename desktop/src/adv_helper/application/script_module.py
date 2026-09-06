from __future__ import annotations

import time

from adv_helper.config import AppConfig
from adv_helper.os_adapters.script_runner import ScriptRunner, ShellScriptRunner
from adv_helper.platform.diagnostics import LocalDiagnostics
from adv_helper.core.messages import RequestMessage, ResponseMessage, Result, ProtocolError, encode_message, error_response
from adv_helper.core.protocol_constants import (SCRIPT_PAGE_SIZE, ACTION_ACTIONS_LIST,
    ACTION_SCRIPTS_EXECUTE, ACTION_SHORTCUT_EXECUTE)
from adv_helper.core.script_contract import unsigned, valid_key, valid_script_id


class ScriptModule:
    def __init__(self, config: AppConfig, runner: ScriptRunner | None = None,
                 diagnostics: LocalDiagnostics | None = None) -> None:
        self._runner = runner or ShellScriptRunner()
        self._diagnostics = diagnostics
        self._cwd = config.config_directory
        self._busy = False
        self.actions = tuple(a for a in config.enabled_actions if a.type == "script")
        self._items = []
        seen: set[str] = set()
        for action in self.actions:
            # Duplicate keys remain valid; the first global match owns the hint.
            effective_key = action.key if action.key not in seen else None
            if action.key is not None:
                seen.add(action.key)
            self._items.append(dict(type="script", actionId=action.action_id, name=action.name,
                                    key=action.key, effectiveKey=effective_key))

    async def list_actions(self, request: RequestMessage) -> ResponseMessage:
        offset = request.payload.get("offset")
        total = len(self.actions)
        if (set(request.payload) != {"offset"} or not unsigned(offset) or offset % SCRIPT_PAGE_SIZE
                or (offset >= total if total else offset != 0)):
            return error_response(request, "ERROR", "invalid page offset")
        end = min(offset + SCRIPT_PAGE_SIZE, total)
        response = ResponseMessage("response", request.action_id, request.exec_id, Result("OK", "actions page", {
            "offset": offset, "total": total, "nextOffset": end if end < total else None,
            "actions": self._items[offset:end],
        }))
        try:
            encode_message(response)
        except ProtocolError:
            # Never make the catalog smaller to fit a transport message.
            return error_response(request, "ERROR", "actions page exceeds transport limit")
        return response

    async def handle(self, request: RequestMessage) -> ResponseMessage:
        if request.action_id == ACTION_ACTIONS_LIST:
            return await self.list_actions(request)
        if request.action_id == ACTION_SCRIPTS_EXECUTE:
            action_id = request.payload.get("actionId")
            if set(request.payload) != {"actionId"} or not valid_script_id(action_id):
                return error_response(request, "ERROR", "invalid script identifier")
            action = next((a for a in self.actions if a.action_id == action_id), None)
        elif request.action_id == ACTION_SHORTCUT_EXECUTE:
            key = request.payload.get("key")
            if set(request.payload) != {"key"} or key is None or not valid_key(key):
                return error_response(request, "ERROR", "invalid shortcut key")
            # Resolve against the entire ordered configuration, never a BLE page.
            action = next((a for a in self.actions if a.key == key), None)
        else:
            return error_response(request, "ERROR", "unknown script operation")
        if action is None:
            return error_response(request, "ERROR", "未绑定快捷键" if request.action_id == ACTION_SHORTCUT_EXECUTE else "script unavailable")
        if self._busy:
            return error_response(request, "BUSY", "script already running")

        # Occupy before any await, and release only after runner cancellation cleanup.
        self._busy = True
        started = time.monotonic()
        data = {"actionId": action.action_id, "name": action.name, "exitCode": None}
        try:
            result = await self._runner.run(action.content, self._cwd, action.params["timeoutSeconds"])
            data["exitCode"] = result.exit_code
            if self._diagnostics:
                self._diagnostics.info("script completed", actionId=action.action_id, execId=request.exec_id,
                                       resultCode=result.code, exitCode=result.exit_code,
                                       durationMs=round((time.monotonic() - started) * 1000))
            return ResponseMessage("response", request.action_id, request.exec_id,
                                   Result(result.code, {"OK": "已执行", "TIMEOUT": "脚本执行超时", "ERROR": "脚本执行失败"}[result.code], data))
        except Exception as error:
            if self._diagnostics:
                self._diagnostics.error("script failed", actionId=action.action_id, execId=request.exec_id,
                                        errorType=type(error).__name__)
            return ResponseMessage("response", request.action_id, request.exec_id, Result("ERROR", "脚本执行失败", data))
        finally:
            self._busy = False
