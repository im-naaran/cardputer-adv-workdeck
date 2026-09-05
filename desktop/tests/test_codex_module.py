import asyncio

import pytest

from adv_helper.application.codex_module import CodexModule
from adv_helper.core.messages import RequestMessage
from adv_helper.core.registry import ActionRegistry
from adv_helper.os_adapters.codex_app_server import (
    CodexTimeoutError,
    CodexUnavailableError,
    InvalidCodexResponseError,
    NotLoggedInError,
    UsageSnapshot,
    UsageWindow,
)


class FakeProvider:
    def __init__(self, result=None, error=None):
        self.result = result or UsageSnapshot(100, ())
        self.error = error
        self.calls = 0

    async def read_usage(self):
        self.calls += 1
        if self.error:
            raise self.error
        return self.result


def request(exec_id="exec-1", action_id="codex.usage.read"):
    return RequestMessage("request", action_id, exec_id, {})


@pytest.mark.asyncio
async def test_idle_module_never_calls_provider_and_registry_exposes_capability():
    provider = FakeProvider()
    module = CodexModule(provider)
    registry = ActionRegistry()
    registry.register("codex.usage.read", module.handle)
    await asyncio.sleep(0)
    assert provider.calls == 0
    assert registry.capabilities == ("codex.usage.read",)


@pytest.mark.asyncio
async def test_one_valid_request_reads_once_and_preserves_exec_id_and_boundaries():
    snapshot = UsageSnapshot(
        123,
        (
            UsageWindow("codex", None, "primary", -1, 300, 1000),
            UsageWindow("codex", None, "secondary", 101, None, None),
        ),
    )
    provider = FakeProvider(snapshot)
    response = await CodexModule(provider).handle(request("same-id"))
    assert provider.calls == 1
    assert response.exec_id == "same-id"
    assert [item["usedPercent"] for item in response.result.data["windows"]] == [-1, 101]


@pytest.mark.asyncio
async def test_invalid_action_does_not_call_provider():
    provider = FakeProvider()
    response = await CodexModule(provider).handle(request(action_id="other"))
    assert response.result.code == "ERROR"
    assert provider.calls == 0


@pytest.mark.asyncio
async def test_concurrent_request_returns_busy():
    started = asyncio.Event()
    release = asyncio.Event()

    class BlockingProvider(FakeProvider):
        async def read_usage(self):
            self.calls += 1
            started.set()
            await release.wait()
            return self.result

    provider = BlockingProvider()
    module = CodexModule(provider)
    first = asyncio.create_task(module.handle(request("first")))
    await started.wait()
    second = await module.handle(request("second"))
    release.set()
    assert (await first).result.code == "OK"
    assert second.result.code == "BUSY"
    assert provider.calls == 1


@pytest.mark.parametrize(
    ("error", "code"),
    [
        (NotLoggedInError(), "NOT_LOGGED_IN"),
        (CodexTimeoutError(), "TIMEOUT"),
        (CodexUnavailableError(), "CODEX_UNAVAILABLE"),
        (InvalidCodexResponseError(), "INVALID_RESPONSE"),
        (RuntimeError(), "ERROR"),
    ],
)
@pytest.mark.asyncio
async def test_error_mapping(error, code):
    response = await CodexModule(FakeProvider(error=error)).handle(request())
    assert response.result.code == code
