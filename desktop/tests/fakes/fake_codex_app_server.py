from __future__ import annotations

import asyncio
import json
from collections.abc import Iterable
from typing import Any


class FakeStdin:
    def __init__(self) -> None:
        self.lines: list[dict[str, Any]] = []

    def write(self, data: bytes) -> None:
        self.lines.append(json.loads(data))


class FakeStdout:
    def __init__(self, responses: Iterable[dict[str, Any] | bytes | None]) -> None:
        self.responses = list(responses)

    async def readline(self) -> bytes:
        if not self.responses:
            return b""
        response = self.responses.pop(0)
        if response is None:
            await asyncio.Event().wait()
        if isinstance(response, bytes):
            return response
        return json.dumps(response).encode() + b"\n"


class FakeProcess:
    def __init__(self, responses: Iterable[dict[str, Any] | bytes | None]) -> None:
        self.stdin = FakeStdin()
        self.stdout = FakeStdout(responses)
        self.returncode: int | None = None
        self.terminated = False

    def terminate(self) -> None:
        self.terminated = True
        self.returncode = 0

    async def wait(self) -> int:
        return self.returncode or 0


class FakeProcessFactory:
    def __init__(self, *processes: FakeProcess) -> None:
        self.processes = list(processes)
        self.calls = 0

    async def __call__(self) -> FakeProcess:
        self.calls += 1
        if not self.processes:
            raise FileNotFoundError("no fake process")
        return self.processes.pop(0)


def response(request_id: int, result: dict[str, Any]) -> dict[str, Any]:
    return {"id": request_id, "result": result}


def chatgpt_account() -> dict[str, Any]:
    return {"account": {"type": "chatgpt", "email": "hidden@example.com"}, "requiresOpenaiAuth": True}
