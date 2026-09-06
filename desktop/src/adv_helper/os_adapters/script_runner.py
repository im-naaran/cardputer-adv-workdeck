from __future__ import annotations

import asyncio
import os
import signal
from dataclasses import dataclass
from pathlib import Path
from typing import Protocol


@dataclass(frozen=True)
class ScriptRunResult:
    code: str
    exit_code: int | None = None


class ScriptRunner(Protocol):
    async def run(self, content: str, cwd: Path, timeout_seconds: int) -> ScriptRunResult: ...


async def _finish(task: asyncio.Task):
    # Repeated session cancellation must not abandon a spawned child or its reaper.
    while True:
        try:
            return await asyncio.shield(task)
        except asyncio.CancelledError:
            if task.done():
                return task.result()


class ShellScriptRunner:
    def __init__(self, *, terminate_grace_seconds: float = 1, kill_wait_seconds: float = 1) -> None:
        self.terminate_grace_seconds = terminate_grace_seconds
        self.kill_wait_seconds = kill_wait_seconds

    async def run(self, content: str, cwd: Path, timeout_seconds: int) -> ScriptRunResult:
        # No terminal or captured output: scripts cannot stall on stdin or fill pipes.
        spawning = asyncio.create_task(asyncio.create_subprocess_exec(
            "/bin/sh", "-c", content, cwd=cwd,
            stdin=asyncio.subprocess.DEVNULL, stdout=asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL, start_new_session=True,
        ))
        try:
            process = await asyncio.shield(spawning)
        except asyncio.CancelledError:
            try:
                process = await _finish(spawning)
            except OSError:
                raise asyncio.CancelledError from None
            await _finish(asyncio.create_task(self._terminate(process)))
            raise
        except OSError:
            return ScriptRunResult("ERROR")

        try:
            exit_code = await asyncio.wait_for(process.wait(), timeout_seconds)
        except (TimeoutError, asyncio.CancelledError) as error:
            # Killing a shell alone leaves its children alive. This does not undo
            # effects already performed or tasks that deliberately detached.
            cleanup = asyncio.create_task(self._terminate(process))
            try:
                await asyncio.shield(cleanup)
            except asyncio.CancelledError:
                await _finish(cleanup)
                raise
            if isinstance(error, asyncio.CancelledError):
                raise
            return ScriptRunResult("TIMEOUT")
        return ScriptRunResult("OK" if exit_code == 0 else "ERROR", exit_code)

    async def _terminate(self, process: asyncio.subprocess.Process) -> None:
        self._signal_group(process.pid, signal.SIGTERM)
        deadline = asyncio.get_running_loop().time() + self.terminate_grace_seconds
        # A shell can exit before its TERM-resistant child; check the group too.
        while self._group_exists(process.pid) and asyncio.get_running_loop().time() < deadline:
            await asyncio.sleep(min(0.02, self.terminate_grace_seconds))
        if self._group_exists(process.pid):
            self._signal_group(process.pid, signal.SIGKILL)
        await asyncio.wait_for(process.wait(), self.kill_wait_seconds)

    @staticmethod
    def _signal_group(pid: int, sig: int) -> None:
        try:
            os.killpg(pid, sig)
        except ProcessLookupError:
            pass

    @staticmethod
    def _group_exists(pid: int) -> bool:
        try:
            os.killpg(pid, 0)
            return True
        except ProcessLookupError:
            return False
