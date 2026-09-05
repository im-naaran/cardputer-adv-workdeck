import asyncio
import io
import logging

import pytest

from adv_helper.bootstrap import DesktopApplication, ModuleManager, build_application
from adv_helper.config import parse_config
from adv_helper.core.messages import RequestMessage, ResponseMessage, Result, decode_message, encode_message
from adv_helper.core.registry import ActionRegistry
from adv_helper.os_adapters.codex_app_server import UsageSnapshot
from adv_helper.os_adapters.computer_identity import ComputerIdentity
from adv_helper.platform.diagnostics import LocalDiagnostics


class FakeProvider:
    def __init__(self):
        self.calls = 0
        self.closed = False

    async def read_usage(self):
        self.calls += 1
        return UsageSnapshot(123, ())

    async def close(self):
        self.closed = True


class MemoryTransport:
    def __init__(self):
        self.incoming = asyncio.Queue()
        self.sent = []
        self.sent_event = asyncio.Event()

    async def send(self, payload):
        self.sent.append(payload)
        self.sent_event.set()

    async def receive_chunk(self):
        value = await self.incoming.get()
        if isinstance(value, Exception):
            raise value
        return value

    async def wait_for_sent(self, count):
        while len(self.sent) < count:
            self.sent_event.clear()
            await self.sent_event.wait()


def config(enabled=True):
    return parse_config({
        "configVersion": 1,
        "actions": [{
            "type": "codex",
            "actionId": "codex.usage.read",
            "name": "Codex usage",
            "key": None,
            "enabled": enabled,
            "content": "usage",
            "params": {},
        }],
    })


def quiet_diagnostics():
    logger = logging.getLogger(f"test-{id(object())}")
    logger.addHandler(logging.NullHandler())
    return LocalDiagnostics(logger)


IDENTITY = ComputerIdentity("stable-test-id", "Test Mac")


def build_test_application(app_config, provider, **kwargs):
    return build_application(
        app_config,
        quiet_diagnostics(),
        provider=provider,
        computer_identity=IDENTITY,
        **kwargs,
    )


@pytest.mark.asyncio
async def test_hello_then_one_request_and_disconnect_cleanup():
    provider = FakeProvider()
    app = build_test_application(config(), provider)
    transport = MemoryTransport()
    task = asyncio.create_task(app.run_session(transport))
    await transport.wait_for_sent(1)
    hello = decode_message(transport.sent[0][:-1])
    assert isinstance(hello, ResponseMessage)
    assert hello.action_id == "system.hello"
    assert "settings" not in hello.result.data
    assert hello.result.data["capabilities"] == ["codex.usage.read", "system.time.read"]

    request = RequestMessage("request", "codex.usage.read", "exec-1", {})
    await transport.incoming.put(encode_message(request))
    await transport.wait_for_sent(2)
    response = decode_message(transport.sent[1][:-1])
    assert isinstance(response, ResponseMessage)
    assert response.exec_id == "exec-1"
    assert response.result.code == "OK"
    assert provider.calls == 1

    await transport.incoming.put(ConnectionError("disconnected"))
    with pytest.raises(ConnectionError):
        await task
    assert not app.connection_session.connected


@pytest.mark.asyncio
async def test_idle_session_never_queries_provider():
    provider = FakeProvider()
    app = build_test_application(config(), provider)
    transport = MemoryTransport()
    task = asyncio.create_task(app.run_session(transport))
    await transport.wait_for_sent(1)
    await asyncio.sleep(0)
    await transport.incoming.put(ConnectionError("disconnected"))
    with pytest.raises(ConnectionError):
        await task
    assert provider.calls == 0


@pytest.mark.asyncio
async def test_disabled_action_is_not_advertised_or_called():
    provider = FakeProvider()
    app = build_test_application(config(False), provider)
    transport = MemoryTransport()
    task = asyncio.create_task(app.run_session(transport))
    await transport.wait_for_sent(1)
    hello = decode_message(transport.sent[0][:-1])
    assert hello.result.data["capabilities"] == ["system.time.read"]
    await transport.incoming.put(ConnectionError("disconnected"))
    with pytest.raises(ConnectionError):
        await task
    assert provider.calls == 0


@pytest.mark.asyncio
async def test_broken_module_isolated_and_other_handler_remains_available():
    registry = ActionRegistry()
    diagnostics = quiet_diagnostics()
    manager = ModuleManager(registry, diagnostics)

    def broken_factory():
        raise RuntimeError("init failed")

    manager.enable("broken", broken_factory)

    async def healthy(request):
        return ResponseMessage("response", request.action_id, request.exec_id, Result("OK", "healthy", {}))

    registry.register("healthy", healthy)
    response = await registry.dispatch(RequestMessage("request", "healthy", "1", {}))
    assert response.result.code == "OK"
    assert registry.capabilities == ("healthy",)


@pytest.mark.asyncio
async def test_second_request_returns_busy_while_first_query_is_running():
    class BlockingProvider(FakeProvider):
        def __init__(self):
            super().__init__()
            self.started = asyncio.Event()
            self.release = asyncio.Event()

        async def read_usage(self):
            self.calls += 1
            self.started.set()
            await self.release.wait()
            return UsageSnapshot(123, ())

    provider = BlockingProvider()
    app = build_test_application(config(), provider)
    transport = MemoryTransport()
    session_task = asyncio.create_task(app.run_session(transport))
    await transport.wait_for_sent(1)
    first = RequestMessage("request", "codex.usage.read", "first", {})
    second = RequestMessage("request", "codex.usage.read", "second", {})
    await transport.incoming.put(encode_message(first) + encode_message(second))
    await provider.started.wait()
    await transport.wait_for_sent(2)
    early_response = decode_message(transport.sent[1][:-1])
    assert early_response.exec_id == "second"
    assert early_response.result.code == "BUSY"
    provider.release.set()
    await transport.wait_for_sent(3)
    completed = decode_message(transport.sent[2][:-1])
    assert completed.exec_id == "first"
    assert completed.result.code == "OK"
    assert provider.calls == 1
    await transport.incoming.put(ConnectionError("disconnected"))
    with pytest.raises(ConnectionError):
        await session_task


@pytest.mark.asyncio
async def test_incomplete_frame_expires_without_a_followup_chunk():
    provider = FakeProvider()
    app = build_test_application(
        config(),
        provider,
        receive_poll_seconds=0.005,
        frame_timeout_seconds=0.01,
    )
    transport = MemoryTransport()
    session_task = asyncio.create_task(app.run_session(transport))
    await transport.wait_for_sent(1)
    await transport.incoming.put(b'{"incomplete":')
    await asyncio.sleep(0.03)
    request = RequestMessage("request", "codex.usage.read", "after-timeout", {})
    await transport.incoming.put(encode_message(request))
    await transport.wait_for_sent(2)
    response = decode_message(transport.sent[1][:-1])
    assert response.exec_id == "after-timeout"
    await transport.incoming.put(ConnectionError("disconnected"))
    with pytest.raises(ConnectionError):
        await session_task


def test_diagnostics_redacts_auth_shaped_fields():
    stream = io.StringIO()
    logger = logging.getLogger("redaction-test")
    logger.handlers.clear()
    logger.propagate = False
    handler = logging.StreamHandler(stream)
    logger.addHandler(handler)
    logger.setLevel(logging.INFO)
    LocalDiagnostics(logger).info("event", accessToken="very-secret", nested={"password": "hidden"})
    output = stream.getvalue()
    assert "very-secret" not in output
    assert "hidden" not in output
    assert output.count("[REDACTED]") == 2


@pytest.mark.asyncio
async def test_time_available_when_codex_disabled():
    provider = FakeProvider()
    app = build_test_application(config(False), provider)
    result = await app.registry.dispatch(RequestMessage("request", "system.time.read", "time", {}))
    assert result.result.code == "OK"
    assert type(result.result.data["epochMilliseconds"]) is int
    assert provider.calls == 0


@pytest.mark.asyncio
async def test_slow_codex_does_not_block_time_and_messages_are_serialized():
    started, release = asyncio.Event(), asyncio.Event()
    class BlockingProvider(FakeProvider):
        async def read_usage(self):
            started.set()
            await release.wait()
            return UsageSnapshot(123, ())
    class FragmentTransport(MemoryTransport):
        def __init__(self):
            super().__init__()
            self.wire = bytearray()
        async def send(self, payload):
            for index in range(0, len(payload), 20):
                self.wire.extend(payload[index:index+20])
                await asyncio.sleep(0)
            await super().send(payload)
    app = build_test_application(config(), BlockingProvider())
    transport = FragmentTransport()
    session = asyncio.create_task(app.run_session(transport))
    try:
        await asyncio.wait_for(transport.wait_for_sent(1), 2)
        await transport.incoming.put(encode_message(RequestMessage("request", "codex.usage.read", "codex", {})))
        await asyncio.wait_for(started.wait(), 2)
        await transport.incoming.put(encode_message(RequestMessage("request", "system.time.read", "time", {})))
        await asyncio.wait_for(transport.wait_for_sent(2), 2)
        assert decode_message(transport.sent[1][:-1]).exec_id == "time"
        release.set()
        await asyncio.wait_for(transport.wait_for_sent(3), 2)
        assert [decode_message(line).exec_id for line in transport.wire.splitlines()][1:] == ["time", "codex"]
    finally:
        session.cancel()
        await asyncio.gather(session, return_exceptions=True)
