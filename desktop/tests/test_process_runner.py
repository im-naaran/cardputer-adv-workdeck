import asyncio
import os
import sys

import pytest

from adv_helper.os_adapters.process_runner import ProcessRunner


async def test_real_stdin_is_data_and_status_output_is_bounded(tmp_path):
    text = "  中文🙂\r\n\t`touch escaped` $(touch escaped)  ".encode()
    code = "import sys,pathlib; pathlib.Path('input').write_bytes(sys.stdin.buffer.read()); print('WRITTEN')"
    runner = ProcessRunner()
    result = await runner.execute((sys.executable, '-c', code), cwd=tmp_path,
                                  timeout_seconds=2, input_data=text, output_limit=32)
    assert result.code == 'OK' and result.output == b'WRITTEN\n'
    assert (tmp_path / 'input').read_bytes() == text
    assert not (tmp_path / 'escaped').exists()
    result = await runner.execute((sys.executable, '-c', "print('x'*10000)"),
                                  timeout_seconds=2, output_limit=16)
    assert result.code == 'ERROR' and result.output == b''
    assert runner.available


async def test_stdin_backpressure_timeout_reaps_real_child(tmp_path):
    code = "import os,time,pathlib; pathlib.Path('pid').write_text(str(os.getpid())); time.sleep(30)"
    runner = ProcessRunner(terminate_grace_seconds=0.02)
    result = await runner.execute((sys.executable, '-c', code), cwd=tmp_path,
                                  timeout_seconds=0.3, input_data=b'x' * 2_000_000)
    assert result.code == 'TIMEOUT'
    with pytest.raises(ProcessLookupError):
        os.kill(int((tmp_path / 'pid').read_text()), 0)
    assert runner.available


@pytest.mark.parametrize('cancel', [False, True])
async def test_spawn_timeout_or_repeated_cancel_retains_child_ownership(monkeypatch, cancel):
    started, release = asyncio.Event(), asyncio.Event()
    cleaned = []
    class Process:
        pid = 123
    async def spawn(*args, **kwargs):
        started.set()
        await release.wait()
        return Process()
    runner = ProcessRunner()
    async def cleanup(process):
        cleaned.append(process.pid)
    monkeypatch.setattr(asyncio, 'create_subprocess_exec', spawn)
    monkeypatch.setattr(runner, '_terminate', cleanup)
    task = asyncio.create_task(runner.execute(('fixed-program',), timeout_seconds=0.01))
    await started.wait()
    if cancel:
        task.cancel()
        await asyncio.sleep(0)
        task.cancel()
    else:
        await asyncio.sleep(0.03)
    assert not task.done()
    release.set()
    if cancel:
        with pytest.raises(asyncio.CancelledError):
            await task
    else:
        assert (await task).code == 'TIMEOUT'
    assert cleaned == [123] and runner.available


async def test_failed_cleanup_disables_further_spawns(monkeypatch):
    calls = []
    class Process:
        pid = 123
        async def wait(self):
            await asyncio.Event().wait()
    async def spawn(*args, **kwargs):
        calls.append(args)
        return Process()
    async def cleanup(process):
        raise TimeoutError
    runner = ProcessRunner()
    monkeypatch.setattr(asyncio, 'create_subprocess_exec', spawn)
    monkeypatch.setattr(runner, '_terminate', cleanup)
    assert (await runner.execute(('fixed',), timeout_seconds=0.01)).code == 'TIMEOUT'
    assert not runner.available
    assert (await runner.execute(('fixed',), timeout_seconds=1)).code == 'ERROR'
    assert len(calls) == 1


async def test_repeated_cancel_waits_for_cleanup(monkeypatch):
    waiting, cleaning, release = asyncio.Event(), asyncio.Event(), asyncio.Event()
    class Process:
        pid = 123
        async def wait(self):
            waiting.set()
            await asyncio.Event().wait()
    async def spawn(*args, **kwargs): return Process()
    async def cleanup(process):
        cleaning.set()
        await release.wait()
    runner = ProcessRunner()
    monkeypatch.setattr(asyncio, 'create_subprocess_exec', spawn)
    monkeypatch.setattr(runner, '_terminate', cleanup)
    task = asyncio.create_task(runner.execute(('fixed',), timeout_seconds=10))
    await waiting.wait()
    task.cancel()
    await cleaning.wait()
    task.cancel()
    await asyncio.sleep(0)
    assert not task.done()
    release.set()
    with pytest.raises(asyncio.CancelledError):
        await task
    assert runner.available


async def test_fixed_argv_and_devnull_stderr(monkeypatch):
    captured = {}
    class Process:
        pid = 123
        async def wait(self): return 0
    async def spawn(*args, **kwargs):
        captured.update(argv=args, **kwargs)
        return Process()
    monkeypatch.setattr(asyncio, 'create_subprocess_exec', spawn)
    assert (await ProcessRunner().execute(('fixed', '-e', 'program'), timeout_seconds=1)).code == 'OK'
    assert captured['argv'] == ('fixed', '-e', 'program')
    assert captured['stdin'] == captured['stdout'] == captured['stderr'] == asyncio.subprocess.DEVNULL
    assert captured['start_new_session'] is True


async def test_cancel_while_writing_closes_io_and_reaps(monkeypatch):
    writing = asyncio.Event()
    cleaned = []
    class Input:
        def write(self, data): assert data == b'snippet'
        async def drain(self):
            writing.set()
            await asyncio.Event().wait()
    class Process:
        pid = 123
        stdin = Input()
        async def wait(self): await asyncio.Event().wait()
    async def spawn(*args, **kwargs): return Process()
    async def cleanup(process): cleaned.append(process.pid)
    runner = ProcessRunner()
    monkeypatch.setattr(asyncio, 'create_subprocess_exec', spawn)
    monkeypatch.setattr(runner, '_terminate', cleanup)
    task = asyncio.create_task(runner.execute(('fixed',), timeout_seconds=10, input_data=b'snippet'))
    await writing.wait()
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await task
    assert cleaned == [123]


async def test_unexpected_wait_error_still_cleans_up(monkeypatch):
    cleaned = []
    class Process:
        pid = 123
        async def wait(self): raise RuntimeError('transport failed')
    async def spawn(*args, **kwargs): return Process()
    async def cleanup(process): cleaned.append(process.pid)
    runner = ProcessRunner()
    monkeypatch.setattr(asyncio, 'create_subprocess_exec', spawn)
    monkeypatch.setattr(runner, '_terminate', cleanup)
    result = await runner.execute(('fixed',), timeout_seconds=1)
    assert result.code == 'ERROR' and cleaned == [123]
