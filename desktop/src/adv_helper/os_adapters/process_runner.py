"""POSIX child lifetime management shared by shell and macOS fixed programs."""
from __future__ import annotations

import asyncio
import os
import signal
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class ProcessResult:
    code: str
    exit_code: int | None = None
    output: bytes = b""


async def _finish(task: asyncio.Future):
    # Repeated session cancellation must not abandon a child still being created/reaped.
    while True:
        try:
            return await asyncio.shield(task)
        except asyncio.CancelledError:
            if task.done():
                return task.result()


class ProcessRunner:
    def __init__(self, *, terminate_grace_seconds: float = 1, kill_wait_seconds: float = 1) -> None:
        self.terminate_grace_seconds = terminate_grace_seconds
        self.kill_wait_seconds = kill_wait_seconds
        self.available = True

    async def execute(self, argv: tuple[str, ...], *, timeout_seconds: float,
                      cwd: Path | None = None, input_data: bytes | None = None,
                      output_limit: int = 0) -> ProcessResult:
        if not self.available:
            return ProcessResult("ERROR")
        if output_limit < 0:
            raise ValueError("output limit must not be negative")
        # argv is code chosen by the caller; snippet text belongs exclusively in stdin.
        spawning = asyncio.create_task(asyncio.create_subprocess_exec(
            *argv, cwd=cwd, stdin=asyncio.subprocess.PIPE if input_data is not None else asyncio.subprocess.DEVNULL,
            stdout=asyncio.subprocess.PIPE if output_limit else asyncio.subprocess.DEVNULL,
            stderr=asyncio.subprocess.DEVNULL, start_new_session=True,
        ))
        process = None
        try:
            async with asyncio.timeout(timeout_seconds):
                process = await asyncio.shield(spawning)
                if input_data is None and not output_limit:
                    exit_code = await process.wait()
                    output = b""
                else:
                    exit_code, output = await self._exchange(process, input_data, output_limit)
            return ProcessResult("OK" if exit_code == 0 else "ERROR", exit_code, output)
        except (Exception, asyncio.CancelledError) as error:
            # A timeout while spawning has the same ownership duty as cancellation.
            cleanup = asyncio.create_task(self._cleanup(spawning, process))
            cancelled = isinstance(error, asyncio.CancelledError)
            try:
                await asyncio.shield(cleanup)
            except asyncio.CancelledError:
                cancelled = True
                await _finish(cleanup)
            if cancelled:
                raise asyncio.CancelledError from None
            return ProcessResult("TIMEOUT" if isinstance(error, TimeoutError) else "ERROR")

    async def _exchange(self, process, input_data: bytes | None, output_limit: int) -> tuple[int, bytes]:
        async def write_input():
            if input_data is not None:
                process.stdin.write(input_data)
                await process.stdin.drain()
                process.stdin.close()
                await process.stdin.wait_closed()

        async def read_output():
            output = bytearray()
            if output_limit:
                while chunk := await process.stdout.read(min(1024, output_limit + 1 - len(output))):
                    output.extend(chunk)
                    if len(output) > output_limit:
                        raise ValueError("child status output exceeds limit")
            return bytes(output)

        # Drain status concurrently with stdin so a full pipe cannot deadlock either side.
        tasks = [asyncio.create_task(write_input()), asyncio.create_task(read_output()),
                 asyncio.create_task(process.wait())]
        try:
            _, output, exit_code = await asyncio.gather(*tasks)
            return exit_code, output
        finally:
            for task in tasks:
                task.cancel()
            await _finish(asyncio.gather(*tasks, return_exceptions=True))

    async def _cleanup(self, spawning, process) -> None:
        try:
            if process is None:
                try:
                    process = await spawning
                except OSError:
                    return  # Spawn failed before there was a child to own.
            await self._terminate(process)
        except Exception:
            # Do not issue another OS operation when cleanup could not be confirmed.
            self.available = False

    async def _terminate(self, process) -> None:
        self._signal_group(process.pid, signal.SIGTERM)
        loop = asyncio.get_running_loop()
        deadline = loop.time() + self.terminate_grace_seconds
        while self._group_exists(process.pid) and loop.time() < deadline:
            await asyncio.sleep(min(0.02, max(0, deadline - loop.time())))
        if self._group_exists(process.pid):
            self._signal_group(process.pid, signal.SIGKILL)
        # Reap the leader and confirm descendants disappeared within the same kill budget.
        async with asyncio.timeout(self.kill_wait_seconds):
            await process.wait()
            while self._group_exists(process.pid):
                await asyncio.sleep(0.01)

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
