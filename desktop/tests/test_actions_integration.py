import asyncio

import pytest

from adv_helper.application import actions_module
from adv_helper.application.actions_module import ActionsModule
from adv_helper.os_adapters.paste_runner import PasteResult, UnavailablePasteRunner
from adv_helper.core.messages import decode_message, encode_message
from test_clipboard_config import item
from test_script_config import configured, script
from test_script_integration import Runner, request
from test_desktop_integration import FakeProvider, build_test_application, MemoryTransport


class Paste:
    available = True
    def __init__(self, result=PasteResult('OK', None, True, True)):
        self.calls = []
        self.result = result
    async def paste(self, text):
        self.calls.append(text)
        return self.result


async def test_global_key_ownership_precedes_type_filter_and_pagination():
    paste = Paste()
    scripts = Runner()
    entries = [item(actionId=f'clipboard.test.{i}', content=f'text {i}', key='g') for i in range(17)]
    module = ActionsModule(configured(entries[0], script(), *entries[1:]), scripts, paste_runner=paste)
    for kind, count in [('script', 1), ('clipboard', 17)]:
        offset, items = 0, []
        while offset is not None:
            result = await module.handle(request('actions.list', {'type': kind, 'offset': offset}))
            data = decode_message(encode_message(result)[:-1]).result.data
            assert data['total'] == count and data['type'] == kind
            items.extend(data['actions'])
            offset = data['nextOffset']
        assert len(items) == count
        assert [i['effectiveKey'] for i in items] == (['g'] + [None]*16 if kind == 'clipboard' else [None])
    result = await module.handle(request('actions.shortcut.execute', {'key': 'g'}))
    assert result.result.data['type'] == 'clipboard' and paste.calls == ['text 0']
    await module.handle(request('actions.execute', {'actionId': 'script.test.0'}))
    await module.handle(request('actions.execute', {'actionId': 'clipboard.test.16'}))
    assert len(scripts.calls) == 1 and paste.calls == ['text 0', 'text 16']


async def test_unavailable_first_key_does_not_fall_back_to_script():
    scripts = Runner()
    module = ActionsModule(configured(item(key='g'), script()), scripts, paste_runner=UnavailablePasteRunner())
    result = await module.handle(request('actions.shortcut.execute', {'key': 'g'}))
    assert result.result.data['reason'] == 'UNAVAILABLE' and not scripts.calls
    assert module.supported_action_types == ('script',)
    assert (await module.handle(request('actions.list', {'type': 'clipboard', 'offset': 0}))).result.code == 'ERROR'


@pytest.mark.parametrize('first_type', ['script', 'clipboard'])
async def test_cross_type_busy_and_cancel_release(first_type):
    started, release = asyncio.Event(), asyncio.Event()
    class BlockingPaste(Paste):
        async def paste(self, text):
            self.calls.append(text)
            if first_type == 'clipboard':
                started.set()
                await release.wait()
            return self.result
    class BlockingScript(Runner):
        async def run(self, *args):
            self.calls.append(args)
            if first_type == 'script':
                started.set()
                await release.wait()
            return self.result
    paste, scripts = BlockingPaste(), BlockingScript()
    module = ActionsModule(configured(script(), item()), scripts, paste_runner=paste)
    ids = {'script': 'script.test.0', 'clipboard': 'clipboard.test'}
    first = asyncio.create_task(module.handle(request('actions.execute', {'actionId': ids[first_type]})))
    await started.wait()
    other = 'clipboard' if first_type == 'script' else 'script'
    result = await module.handle(request('actions.execute', {'actionId': ids[other]}))
    assert result.result.code == 'BUSY'
    assert (await module.handle(request('actions.list', {'type': 'clipboard', 'offset': 0}))).result.code == 'OK'
    first.cancel()
    with pytest.raises(asyncio.CancelledError): await first
    release.set()
    result = await module.handle(request('actions.execute', {'actionId': ids[other]}))
    assert result.result.code == 'OK'


@pytest.mark.parametrize('result', [PasteResult('OK', None, True, True),
    PasteResult('ERROR', 'PASTE_FAILED', True, False),
    PasteResult('TIMEOUT', 'UNCONFIRMED', None, False),
    PasteResult('ERROR', 'PERMISSION_DENIED', True, False)])
async def test_paste_outcomes_roundtrip_and_repeat(result):
    paste = Paste(result)
    module = ActionsModule(configured(item()), paste_runner=paste)
    for _ in range(2):
        response = await module.handle(request('actions.execute', {'actionId': 'clipboard.test'}))
        assert decode_message(encode_message(response)[:-1]) == response
        assert response.result.data['clipboardWritten'] is result.clipboard_written
        assert response.result.data['pasteSent'] is result.paste_sent
        assert response.result.data['reason'] == result.reason
    assert len(paste.calls) == 2


async def test_factory_failure_isolated_and_hello_capabilities(monkeypatch):
    def fail(): raise RuntimeError('private details')
    monkeypatch.setattr(actions_module, 'create_paste_runner', fail)
    scripts = Runner()
    app = build_test_application(configured(script(), item()), FakeProvider(), script_runner=scripts)
    hello = app._hello_message()
    assert hello.result.data['protocolVersion'] == 2
    assert hello.result.data['supportedActionTypes'] == ['script']
    assert 'actions.execute' in hello.result.data['capabilities']
    assert 'scripts.execute' not in hello.result.data['capabilities']
    assert (await app.registry.dispatch(request('actions.execute', {'actionId': 'script.test.0'}))).result.code == 'OK'


async def test_clipboard_disconnect_cancels_without_replay():
    started, cancelled = asyncio.Event(), asyncio.Event()
    class Blocking(Paste):
        async def paste(self, text):
            self.calls.append(text)
            started.set()
            try: await asyncio.Event().wait()
            finally: cancelled.set()
    paste = Blocking()
    app = build_test_application(configured(item()), FakeProvider(), paste_runner=paste)
    transport = MemoryTransport()
    session = asyncio.create_task(app.run_session(transport))
    await transport.wait_for_sent(1)
    await transport.incoming.put(encode_message(request('actions.execute', {'actionId': 'clipboard.test'})))
    await asyncio.wait_for(started.wait(), 1)
    # Other fixed capabilities still respond while paste is awaiting OS completion.
    await transport.incoming.put(encode_message(request('system.time.read', {}, 'time')))
    await asyncio.wait_for(transport.wait_for_sent(2), 1)
    session.cancel()
    with pytest.raises(asyncio.CancelledError): await session
    assert cancelled.is_set() and len(paste.calls) == 1
    new_transport = MemoryTransport()
    session = asyncio.create_task(app.run_session(new_transport))
    await new_transport.wait_for_sent(1)
    session.cancel()
    with pytest.raises(asyncio.CancelledError): await session
    assert len(paste.calls) == 1


async def test_script_first_key_and_clipboard_response_send_ownership():
    paste, scripts = Paste(), Runner()
    app = build_test_application(configured(script(), item(key='g')), FakeProvider(),
                                 script_runner=scripts, paste_runner=paste)
    response = await app.registry.dispatch(request('actions.shortcut.execute', {'key': 'g'}))
    assert response.result.data['type'] == 'script' and not paste.calls
    app.connection_session.connect()
    sending, release = asyncio.Event(), asyncio.Event()
    class Slow(MemoryTransport):
        async def send(self, payload):
            sending.set()
            await release.wait()
            await super().send(payload)
    transport = Slow()
    message = request('actions.execute', {'actionId': 'clipboard.test'}, 'same-id')
    first = asyncio.create_task(app._handle_request(transport, message))
    await sending.wait()
    second = asyncio.create_task(app._handle_request(transport, message))
    await asyncio.sleep(0)
    assert len(paste.calls) == 1
    release.set()
    await asyncio.gather(first, second)
    assert [decode_message(line[:-1]).result.code for line in transport.sent] == ['OK', 'BUSY']


async def test_paste_exception_logs_only_error_type_and_recovers():
    import io
    import logging
    from adv_helper.platform.diagnostics import LocalDiagnostics
    stream = io.StringIO()
    logger = logging.getLogger('paste-private-test')
    logger.setLevel(logging.INFO)
    handler = logging.StreamHandler(stream)
    logger.addHandler(handler)
    secret = 'never-log-this-body'
    class Broken(Paste):
        async def paste(self, text): raise OSError(text)
    try:
        module = ActionsModule(configured(item(content=secret)), diagnostics=LocalDiagnostics(logger),
                               paste_runner=Broken())
        for _ in range(2):
            result = await module.handle(request('actions.execute', {'actionId': 'clipboard.test'}))
            assert result.result.code == 'ERROR'
            assert result.result.data['reason'] == 'UNCONFIRMED'
            assert secret not in encode_message(result).decode()
        assert secret not in stream.getvalue()
        assert 'OSError' in stream.getvalue()
    finally:
        logger.removeHandler(handler)


async def test_poisoned_paste_not_retried_and_hello_updates():
    class Poisoned(Paste):
        async def paste(self, text):
            self.calls.append(text)
            self.available = False
            return PasteResult('TIMEOUT', 'UNCONFIRMED', None, False)
    paste = Poisoned()
    app = build_test_application(configured(item()), FakeProvider(), paste_runner=paste)
    assert 'clipboard' in app._hello_message().result.data['supportedActionTypes']
    for _ in range(2):
        await app.registry.dispatch(request('actions.execute', {'actionId': 'clipboard.test'}))
    assert len(paste.calls) == 1
    assert 'clipboard' not in app._hello_message().result.data['supportedActionTypes']


@pytest.mark.parametrize('reason', ['PERMISSION_DENIED', 'PASTE_FAILED', 'UNCONFIRMED'])
async def test_paste_failure_logs_stage_without_text(caplog, reason):
    import logging
    from adv_helper.platform.diagnostics import LocalDiagnostics
    caplog.set_level(logging.INFO, logger='adv_helper')
    secret = 'private-clipboard-body'
    module = ActionsModule(configured(item(content=secret)), diagnostics=LocalDiagnostics(),
                           paste_runner=Paste(PasteResult('ERROR', reason, True, False)))
    await module.handle(request('actions.shortcut.execute', {'key': 'n'}))
    assert f'"reason": "{reason}"' in caplog.text
    assert '"clipboardWritten": true' in caplog.text
    assert '"pasteSent": false' in caplog.text
    assert secret not in caplog.text
