import asyncio

import pytest

from adv_helper.application.script_module import ScriptModule
from adv_helper.core.messages import RequestMessage, decode_message, encode_message
from adv_helper.os_adapters.script_runner import ScriptRunResult
from test_desktop_integration import FakeProvider, MemoryTransport, build_test_application
from test_script_config import configured, script


class Runner:
    def __init__(self, result=ScriptRunResult("OK", 0)):
        self.calls = []
        self.result = result

    async def run(self, content, cwd, timeout):
        self.calls.append((content, cwd, timeout))
        return self.result


def request(action, payload, id="exec"):
    return RequestMessage("request", action, id, payload)


async def test_global_first_match_and_last_id_without_reading_pages():
    runner = Runner()
    config = configured(script(0, enabled=False), *(script(i, content=f"script {i}") for i in range(1, 102)))
    app = build_test_application(config, FakeProvider(), script_runner=runner)
    hello = app._hello_message()
    assert hello.result.data["capabilities"] == ["actions.list", "actions.shortcut.execute", "scripts.execute", "system.time.read"]
    empty_app = build_test_application(configured(), FakeProvider(), script_runner=runner)
    assert len(encode_message(hello)) == len(encode_message(empty_app._hello_message()))
    response = await app.registry.dispatch(request("actions.shortcut.execute", {"key": "g"}))
    assert response.result.data["actionId"] == "script.test.1"
    assert response.action_id == "actions.shortcut.execute"
    response = await app.registry.dispatch(request("scripts.execute", {"actionId": "script.test.101"}))
    assert response.result.data["actionId"] == "script.test.101"
    assert [call[0] for call in runner.calls] == ["script 1", "script 101"]


@pytest.mark.parametrize("action,payload", [
    ("scripts.execute", {"actionId": "script.disabled"}),
    ("scripts.execute", {"actionId": "script.missing"}),
    ("scripts.execute", {"actionId": "script.test.0", "content": "unexpected"}),
    ("actions.shortcut.execute", {"key": "b"}),
    ("actions.shortcut.execute", {"key": None}),
    ("actions.shortcut.execute", {"key": "G"}),
    ("actions.shortcut.execute", {"key": "g", "timeoutSeconds": 1}),
])
async def test_unavailable_and_malformed_requests_never_execute(action, payload):
    runner = Runner()
    module = ScriptModule(configured(script(), script(actionId="script.disabled", enabled=False)), runner)
    response = await module.handle(request(action, payload))
    assert response.result.code == "ERROR" and not runner.calls


@pytest.mark.parametrize("result", [ScriptRunResult("OK", 0), ScriptRunResult("ERROR", 5),
                                    ScriptRunResult("ERROR"), ScriptRunResult("TIMEOUT")])
async def test_execution_outcomes_preserve_actual_identity(result):
    module = ScriptModule(configured(script()), Runner(result))
    response = await module.handle(request("scripts.execute", {"actionId": "script.test.0"}))
    assert decode_message(encode_message(response)[:-1]) == response
    assert response.result.code == result.code
    assert response.result.data["actionId"] == "script.test.0"
    assert response.result.data["exitCode"] == result.exit_code


async def test_runner_failure_keeps_module_available():
    class BrokenRunner(Runner):
        async def run(self, *args): raise OSError("private content")
    module = ScriptModule(configured(script()), BrokenRunner())
    for _ in range(2):
        response = await module.handle(request("actions.shortcut.execute", {"key": "g"}))
        assert response.result.code == "ERROR"
        assert "private" not in response.result.msg


async def test_session_busy_concurrency_and_cancellation_cleanup():
    started, cleaning, release_cleanup = asyncio.Event(), asyncio.Event(), asyncio.Event()
    class BlockingRunner(Runner):
        async def run(self, *args):
            self.calls.append(args)
            started.set()
            try:
                await asyncio.Event().wait()
            finally:
                cleaning.set()
                await release_cleanup.wait()
    runner = BlockingRunner()
    codex = dict(type="codex", actionId="codex.usage.read", name="Codex", key=None,
                 enabled=True, content="usage", params={})
    provider = FakeProvider()
    app = build_test_application(configured(script(), codex), provider, script_runner=runner)
    transport = MemoryTransport()
    session = asyncio.create_task(app.run_session(transport))
    try:
        await asyncio.wait_for(transport.wait_for_sent(1), 2)
        await transport.incoming.put(encode_message(request("actions.shortcut.execute", {"key": "g"}, "first")))
        await asyncio.wait_for(started.wait(), 2)
        for action, payload, id in [
            ("scripts.execute", {"actionId": "script.test.0"}, "second"),
            ("actions.list", {"offset": 0}, "page"), ("system.time.read", {}, "time"),
            ("codex.usage.read", {}, "codex"),
        ]:
            await transport.incoming.put(encode_message(request(action, payload, id)))
        await asyncio.wait_for(transport.wait_for_sent(5), 2)
        responses = {decode_message(line[:-1]).exec_id: decode_message(line[:-1]) for line in transport.sent[1:]}
        assert responses["second"].result.code == "BUSY"
        assert all(responses[id].result.code == "OK" for id in ("page", "time", "codex"))
        assert len(runner.calls) == 1 and provider.calls == 1
        session.cancel()
        await asyncio.wait_for(cleaning.wait(), 2)
        busy = await app.registry.dispatch(request("scripts.execute", {"actionId": "script.test.0"}, "cleanup"))
        assert busy.result.code == "BUSY"
        assert not session.done()
        release_cleanup.set()
        with pytest.raises(asyncio.CancelledError):
            await asyncio.wait_for(session, 2)
        assert not app.connection_session.connected
        assert len(transport.sent) == 5  # No stale completion is sent after cancellation.
    finally:
        release_cleanup.set()
        session.cancel()
        await asyncio.gather(session, return_exceptions=True)


async def test_duplicate_id_stays_busy_until_response_is_sent():
    runner = Runner()
    app = build_test_application(configured(script()), FakeProvider(), script_runner=runner)
    app.connection_session.connect()
    sending, release = asyncio.Event(), asyncio.Event()

    class SlowTransport(MemoryTransport):
        async def send(self, payload):
            sending.set()
            await release.wait()
            await super().send(payload)

    transport = SlowTransport()
    message = request("actions.shortcut.execute", {"key": "g"}, "same-id")
    first = asyncio.create_task(app._handle_request(transport, message))
    second = None
    try:
        await asyncio.wait_for(sending.wait(), 1)
        second = asyncio.create_task(app._handle_request(transport, message))
        await asyncio.sleep(0)  # Second handler reaches the occupied send lock.
        assert len(runner.calls) == 1
        release.set()
        await asyncio.wait_for(asyncio.gather(first, second), 1)
        codes = [decode_message(line[:-1]).result.code for line in transport.sent]
        assert codes == ["OK", "BUSY"]
        assert app.connection_session.begin_request("same-id")
    finally:
        release.set()
        for task in (first, second):
            if task is not None:
                task.cancel()
        await asyncio.gather(*(t for t in (first, second) if t is not None), return_exceptions=True)
