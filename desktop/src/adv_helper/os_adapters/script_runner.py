from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Protocol

from .process_runner import ProcessRunner


@dataclass(frozen=True)
class ScriptRunResult:
    code: str
    exit_code: int | None = None


class ScriptRunner(Protocol):
    async def run(self, content: str, cwd: Path, timeout_seconds: int) -> ScriptRunResult: ...


class ShellScriptRunner(ProcessRunner):
    async def run(self, content: str, cwd: Path, timeout_seconds: int) -> ScriptRunResult:
        # Preserve shell semantics and DEVNULL streams; only child ownership is shared.
        result = await self.execute(("/bin/sh", "-c", content), cwd=cwd, timeout_seconds=timeout_seconds)
        return ScriptRunResult(result.code, result.exit_code)
