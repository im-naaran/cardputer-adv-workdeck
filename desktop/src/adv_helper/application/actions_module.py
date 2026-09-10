from __future__ import annotations

import time

from adv_helper.config import AppConfig
from adv_helper.core.action_contract import ACTION_TYPES, validate_request, validate_execution
from adv_helper.core.messages import RequestMessage, ResponseMessage, Result, ProtocolError, encode_message, error_response
from adv_helper.core.protocol_constants import ACTION_PAGE_SIZE, ACTION_ACTIONS_LIST, ACTION_EXECUTE, ACTION_SHORTCUT_EXECUTE
from adv_helper.os_adapters.script_runner import ScriptRunner, ShellScriptRunner
from adv_helper.os_adapters.paste_runner import PasteRunner, UnavailablePasteRunner, create_paste_runner
from adv_helper.platform.diagnostics import LocalDiagnostics


class ActionsModule:
    def __init__(self, config: AppConfig, runner: ScriptRunner | None = None,
                 diagnostics: LocalDiagnostics | None = None, *, paste_runner: PasteRunner | None = None) -> None:
        self._runner = runner or ShellScriptRunner()
        self._diagnostics = diagnostics
        try:
            self._paste = paste_runner if paste_runner is not None else create_paste_runner()
        except Exception as error:
            # A missing platform adapter must not disable the shared script core.
            self._paste = UnavailablePasteRunner()
            if diagnostics:
                diagnostics.error('paste initialization failed', errorType=type(error).__name__)
        self._cwd = config.config_directory
        self._busy = False
        self.actions = tuple(a for a in config.enabled_actions if a.type in ACTION_TYPES)
        self._items = []
        seen: set[str] = set()
        for action in self.actions:
            # Ownership is global and precedes UI filtering, even for unavailable types.
            effective_key = action.key if action.key not in seen else None
            if action.key is not None:
                seen.add(action.key)
            self._items.append(dict(type=action.type, actionId=action.action_id, name=action.name,
                                    key=action.key, effectiveKey=effective_key))

    @property
    def supported_action_types(self) -> tuple[str, ...]:
        return tuple(t for t, runner in (('script', self._runner), ('clipboard', self._paste))
                     if getattr(runner, 'available', True))

    async def list_actions(self, request: RequestMessage) -> ResponseMessage:
        try:
            validate_request(ACTION_ACTIONS_LIST, request.payload)
        except ValueError:
            return error_response(request, 'ERROR', '目录参数无效')
        action_type, offset = request.payload['type'], request.payload['offset']
        if action_type not in self.supported_action_types:
            return error_response(request, 'ERROR', '动作类型不可用')
        items = [item for item in self._items if item['type'] == action_type]
        total = len(items)
        if (offset >= total if total else offset != 0):
            return error_response(request, 'ERROR', '目录位置无效')
        end = min(offset + ACTION_PAGE_SIZE, total)
        response = ResponseMessage('response', request.action_id, request.exec_id, Result('OK', '动作目录', {
            'type': action_type, 'offset': offset, 'total': total,
            'nextOffset': end if end < total else None, 'actions': items[offset:end],
        }))
        try:
            encode_message(response)
        except ProtocolError:
            return error_response(request, 'ERROR', '目录超过传输限制')
        return response

    async def handle(self, request: RequestMessage) -> ResponseMessage:
        if request.action_id == ACTION_ACTIONS_LIST:
            return await self.list_actions(request)
        try:
            validate_request(request.action_id, request.payload)
        except ValueError:
            return error_response(request, 'ERROR', '动作参数无效')
        if request.action_id == ACTION_EXECUTE:
            action = next((a for a in self.actions if a.action_id == request.payload['actionId']), None)
        elif request.action_id == ACTION_SHORTCUT_EXECUTE:
            action = next((a for a in self.actions if a.key == request.payload['key']), None)
        else:
            return error_response(request, 'ERROR', '动作不可用')
        if action is None:
            return error_response(request, 'ERROR', '未绑定快捷键' if request.action_id == ACTION_SHORTCUT_EXECUTE else '动作不可用')
        if self._busy:
            return error_response(request, 'BUSY', '动作执行中，请稍候')
        data = dict(type=action.type, actionId=action.action_id, name=action.name, exitCode=None,
                    clipboardWritten=None, pasteSent=None, reason=None)
        if action.type not in self.supported_action_types:
            data['reason'] = 'UNAVAILABLE'
            if action.type == 'clipboard':
                data.update(clipboardWritten=False, pasteSent=False)
            return ResponseMessage('response', request.action_id, request.exec_id, Result('ERROR', '动作不可用', data))

        # Occupy before any await. Adapter cancellation cleanup completes before finally releases.
        self._busy = True
        started = time.monotonic()
        try:
            if action.type == 'script':
                result = await self._runner.run(action.content, self._cwd, action.params['timeoutSeconds'])
                reason = {'OK': None, 'TIMEOUT': 'UNCONFIRMED', 'ERROR': 'EXECUTION_FAILED'}[result.code]
                data.update(exitCode=result.exit_code, reason=reason)
                msg = {'OK': '已执行', 'TIMEOUT': '结果未确认', 'ERROR': '脚本执行失败'}[result.code]
            else:
                result = await self._paste.paste(action.content)
                data.update(clipboardWritten=result.clipboard_written, pasteSent=result.paste_sent, reason=result.reason)
                if result.code == 'OK':
                    msg = '已发送粘贴'
                elif result.reason == 'PERMISSION_DENIED':
                    msg = '粘贴权限不足，文本已复制' if result.clipboard_written else '粘贴权限不足'
                elif result.code == 'TIMEOUT' or result.reason == 'UNCONFIRMED':
                    msg = '结果未确认，请检查电脑'
                else:
                    msg = '粘贴失败，文本已复制' if result.clipboard_written else '复制失败'
            validate_execution(result.code, data)
            if self._diagnostics:
                self._diagnostics.info('action completed', actionId=action.action_id, execId=request.exec_id,
                                       resultCode=result.code, reason=data['reason'],
                                       clipboardWritten=data['clipboardWritten'], pasteSent=data['pasteSent'],
                                       durationMs=round((time.monotonic() - started) * 1000))
            return ResponseMessage('response', request.action_id, request.exec_id, Result(result.code, msg, data))
        except Exception as error:
            if self._diagnostics:
                self._diagnostics.error('action failed', actionId=action.action_id, execId=request.exec_id,
                                        errorType=type(error).__name__)
            # An unexpected adapter error must not manufacture a confirmed side-effect result.
            data.update(exitCode=None, clipboardWritten=None, pasteSent=None, reason='UNCONFIRMED')
            return ResponseMessage('response', request.action_id, request.exec_id,
                                   Result('ERROR', '结果未确认，请检查电脑', data))
        finally:
            self._busy = False
