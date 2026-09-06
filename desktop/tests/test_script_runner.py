import asyncio
import os
import shlex
import signal
import sys

import pytest

from adv_helper.os_adapters.script_runner import ShellScriptRunner


async def wait_file(path):
    async def poll():
        while not path.exists():
            await asyncio.sleep(0.005)
    await asyncio.wait_for(poll(), 2)


async def test_real_cwd_stdin_and_exit_status(tmp_path):
    runner = ShellScriptRunner()
    result = await runner.run('pwd > cwd.txt; read value; test -z "$value"', tmp_path, 2)
    assert result.code == "OK" and result.exit_code == 0
    assert (tmp_path / "cwd.txt").read_text().strip() == str(tmp_path)
    result = await runner.run("exit 7", tmp_path, 2)
    assert result.code == "ERROR" and result.exit_code == 7
    assert (await runner.run("true", tmp_path / "missing", 2)).code == "ERROR"


async def test_timeout_kills_term_resistant_group_without_blocking(tmp_path):
    runner = ShellScriptRunner(terminate_grace_seconds=0.05)
    # Both shell and child ignore TERM. A delayed marker proves KILL stopped work.
    code = "import signal,time,pathlib; signal.signal(signal.SIGTERM,signal.SIG_IGN); pathlib.Path('ready').touch(); time.sleep(0.8); pathlib.Path('escaped').touch()"
    command = f"trap '' TERM; {shlex.quote(sys.executable)} -c {shlex.quote(code)} & wait"
    task = asyncio.create_task(runner.run(command, tmp_path, 0.2))
    await wait_file(tmp_path / "ready")
    assert not task.done()
    result = await asyncio.wait_for(task, 2)
    assert result.code == "TIMEOUT"
    await asyncio.sleep(0.8)
    assert not (tmp_path / "escaped").exists()


async def test_cancellation_waits_for_reaping(tmp_path):
    runner = ShellScriptRunner(terminate_grace_seconds=0.05)
    task = asyncio.create_task(runner.run("echo $$ > pid; exec sleep 30", tmp_path, 30))
    await wait_file(tmp_path / "pid")
    pid = int((tmp_path / "pid").read_text())
    task.cancel()
    with pytest.raises(asyncio.CancelledError):
        await asyncio.wait_for(task, 2)
    with pytest.raises(ProcessLookupError):
        os.kill(pid, 0)


async def test_cancellation_during_spawn_still_reaps(monkeypatch, tmp_path):
    started, release = asyncio.Event(), asyncio.Event()
    class Process:
        pid = 123
        async def wait(self):
            return 0
    async def spawn(*args, **kwargs):
        started.set()
        await release.wait()
        return Process()
    monkeypatch.setattr(asyncio, "create_subprocess_exec", spawn)
    runner = ShellScriptRunner()
    cleaned = []
    async def cleanup(process):
        cleaned.append(process.pid)
    monkeypatch.setattr(runner, "_terminate", cleanup)
    task = asyncio.create_task(runner.run("true", tmp_path, 1))
    await started.wait()
    task.cancel()
    await asyncio.sleep(0)
    task.cancel()
    release.set()
    with pytest.raises(asyncio.CancelledError):
        await task
    assert cleaned == [123]


async def test_cleanup_escalates_even_if_shell_exited(monkeypatch):
    runner = ShellScriptRunner(terminate_grace_seconds=0)
    signals = []
    monkeypatch.setattr(runner, "_group_exists", lambda pid: True)
    monkeypatch.setattr(runner, "_signal_group", lambda pid, sig: signals.append(sig))
    class Process:
        pid = 123
        async def wait(self): return 0
    await runner._terminate(Process())
    assert signals == [signal.SIGTERM, signal.SIGKILL]
