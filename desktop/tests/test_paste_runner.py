import asyncio

import pytest

from adv_helper.os_adapters import paste_runner as paste
from adv_helper.os_adapters.process_runner import ProcessResult


class FakeProcess:
    available = True

    def __init__(self, *results):
        self.results = list(results) or [ProcessResult('OK', 0, b'OK\n')] * 2
        self.calls = []

    async def execute(self, argv, **kwargs):
        self.calls.append((argv, kwargs))
        return self.results.pop(0)


def runner(process, **kwargs):
    return paste.MacOSPasteRunner(process, write_settle_seconds=0, paste_settle_seconds=0, **kwargs)


@pytest.mark.parametrize('text', ['  中文🙂\r\n\ttext  ', '{\\rtf1 literal}', '%!PS-Adobe literal', ' '])
async def test_exact_stdin_then_fixed_paste(text):
    process = FakeProcess()
    result = await runner(process).paste(text)
    assert result == paste.PasteResult('OK', None, True, True)
    write, send = process.calls
    assert write[0] == ('/usr/bin/osascript', '-l', 'JavaScript', '-e', paste.WRITE_PROGRAM)
    assert write[1]['input_data'] == text.encode('utf-8')
    assert send[0] == ('/usr/bin/osascript', '-e', paste.PASTE_PROGRAM)
    assert 'input_data' not in send[1]
    assert 0 < send[1]['timeout_seconds'] <= write[1]['timeout_seconds'] <= 10
    assert write[1]['output_limit'] == send[1]['output_limit'] == 64


@pytest.mark.parametrize('response,code,reason,written', [
    (ProcessResult('OK', 0, b'FAILED\n'), 'ERROR', 'WRITE_FAILED', False),
    (ProcessResult('OK', 0, b'ERROR:-1743\n'), 'ERROR', 'PERMISSION_DENIED', False),
    (ProcessResult('ERROR', 1), 'ERROR', 'UNCONFIRMED', None),
    (ProcessResult('TIMEOUT'), 'TIMEOUT', 'UNCONFIRMED', None),
    (ProcessResult('OK', 0, b'private raw error'), 'ERROR', 'UNCONFIRMED', None),
])
async def test_write_failure_never_sends_paste(response, code, reason, written):
    process = FakeProcess(response)
    result = await runner(process).paste('text')
    assert result == paste.PasteResult(code, reason, written, False)
    assert len(process.calls) == 1


@pytest.mark.parametrize('response,code,reason,sent', [
    (ProcessResult('OK', 0, b'ERROR:-1743'), 'ERROR', 'PERMISSION_DENIED', False),
    (ProcessResult('OK', 0, b'ERROR:-25211'), 'ERROR', 'PERMISSION_DENIED', False),
    (ProcessResult('OK', 0, b'ERROR:1002'), 'ERROR', 'PASTE_FAILED', False),
    (ProcessResult('OK', 0, b'ERROR:-1712'), 'TIMEOUT', 'UNCONFIRMED', None),
    (ProcessResult('TIMEOUT'), 'TIMEOUT', 'UNCONFIRMED', None),
    (ProcessResult('ERROR', 1), 'ERROR', 'UNCONFIRMED', None),
])
async def test_partial_failure_is_explicit(response, code, reason, sent):
    process = FakeProcess(ProcessResult('OK', 0, b'OK'), response)
    assert await runner(process).paste('text') == paste.PasteResult(code, reason, True, sent)
    assert len(process.calls) == 2


@pytest.mark.parametrize('stage', [0, 1])
async def test_cancel_during_os_stage_never_replays(stage):
    started = asyncio.Event()
    class Blocking(FakeProcess):
        async def execute(self, argv, **kwargs):
            if len(self.calls) == stage:
                self.calls.append((argv, kwargs))
                started.set()
                await asyncio.Event().wait()
            return await super().execute(argv, **kwargs)
    process = Blocking()
    task = asyncio.create_task(runner(process).paste('text'))
    await started.wait()
    task.cancel()
    with pytest.raises(asyncio.CancelledError): await task
    assert len(process.calls) == stage + 1


async def test_timeout_during_settle_does_not_invoke_paste():
    process = FakeProcess()
    adapter = paste.MacOSPasteRunner(process, timeout_seconds=0.01, write_settle_seconds=1)
    assert await adapter.paste('text') == paste.PasteResult('TIMEOUT', 'UNCONFIRMED', True, False)
    assert len(process.calls) == 1


async def test_disabled_process_never_retries():
    process = FakeProcess()
    process.available = False
    assert await runner(process).paste('text') == paste.PasteResult('ERROR', 'UNAVAILABLE', False, False)
    assert not process.calls


@pytest.mark.parametrize('text', ['', '\0', '\ud800', 'x'*8193])
async def test_invalid_text_never_spawns(text):
    process = FakeProcess()
    assert (await runner(process).paste(text)).reason == 'WRITE_FAILED'
    assert not process.calls


@pytest.mark.parametrize('system', ['Windows', 'Linux', 'Darwin'])
def test_platform_factory_does_not_execute_os_operations(monkeypatch, system):
    monkeypatch.setattr(paste.platform, 'system', lambda: system)
    monkeypatch.setattr(paste.os, 'access', lambda *args: True)
    adapter = paste.create_paste_runner()
    assert adapter.available == (system == 'Darwin')


@pytest.mark.parametrize('stage', [0, 1])
async def test_cancel_during_settle_never_starts_another_os_operation(monkeypatch, stage):
    waiting = asyncio.Event()
    waits = 0

    async def settle(_seconds):
        nonlocal waits
        current = waits
        waits += 1
        if current == stage:
            waiting.set()
            await asyncio.Event().wait()

    monkeypatch.setattr(paste.asyncio, 'sleep', settle)
    process = FakeProcess()
    task = asyncio.create_task(paste.MacOSPasteRunner(process).paste('text'))
    await waiting.wait()
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    assert len(process.calls) == stage + 1
