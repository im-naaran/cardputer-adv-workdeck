import pytest

from adv_helper.os_adapters.codex_app_server import (
    CodexAppServerClient,
    CodexTimeoutError,
    CodexUnavailableError,
    InvalidCodexResponseError,
    NotLoggedInError,
)
from fakes.fake_codex_app_server import (
    FakeProcess,
    FakeProcessFactory,
    chatgpt_account,
    response,
)


def successful_responses(rate_limits):
    return [response(1, {}), response(2, chatgpt_account()), response(3, rate_limits)]


@pytest.mark.asyncio
async def test_is_lazy_and_initializes_once_for_multiple_reads():
    process = FakeProcess(successful_responses({"rateLimits": {"limitId": "codex", "primary": None}}) + [
        response(4, chatgpt_account()),
        response(5, {"rateLimits": {"limitId": "codex", "primary": None}}),
    ])
    factory = FakeProcessFactory(process)
    client = CodexAppServerClient(process_factory=factory)
    assert factory.calls == 0
    await client.read_usage()
    await client.read_usage()
    assert factory.calls == 1
    assert [line["method"] for line in process.stdin.lines].count("initialize") == 1


@pytest.mark.asyncio
async def test_multi_bucket_is_sorted_and_secondary_is_expanded():
    limits = {
        "rateLimits": {"limitId": "ignored", "primary": None},
        "rateLimitsByLimitId": {
            "zeta": {"limitId": "zeta", "limitName": "Z", "primary": {"usedPercent": 9}},
            "alpha": {
                "limitId": "alpha",
                "limitName": None,
                "primary": {"usedPercent": -1, "windowDurationMins": 300, "resetsAt": 100},
                "secondary": {"usedPercent": 101, "windowDurationMins": 10080, "resetsAt": 200},
            },
        },
    }
    process = FakeProcess([{"method": "notice", "params": {}}, *successful_responses(limits)])
    client = CodexAppServerClient(process_factory=FakeProcessFactory(process), epoch_clock=lambda: 1234)
    snapshot = await client.read_usage()
    assert snapshot.fetched_at_epoch_seconds == 1234
    assert [(window.limit_id, window.window_kind) for window in snapshot.windows] == [
        ("alpha", "primary"),
        ("alpha", "secondary"),
        ("zeta", "primary"),
    ]
    assert [window.used_percent for window in snapshot.windows] == [-1, 101, 9]


@pytest.mark.asyncio
async def test_single_bucket_and_optional_fields():
    limits = {
        "rateLimits": {"limitId": None, "limitName": None, "primary": {"usedPercent": 25}},
        "rateLimitsByLimitId": None,
    }
    client = CodexAppServerClient(process_factory=FakeProcessFactory(FakeProcess(successful_responses(limits))))
    snapshot = await client.read_usage()
    window = snapshot.windows[0]
    assert window.limit_id == "codex"
    assert window.window_duration_mins is None
    assert window.resets_at_epoch_seconds is None


@pytest.mark.asyncio
async def test_not_logged_in_does_not_read_rate_limits():
    process = FakeProcess([response(1, {}), response(2, {"account": None, "requiresOpenaiAuth": True})])
    client = CodexAppServerClient(process_factory=FakeProcessFactory(process))
    with pytest.raises(NotLoggedInError):
        await client.read_usage()
    assert [line["method"] for line in process.stdin.lines] == ["initialize", "initialized", "account/read"]


@pytest.mark.asyncio
async def test_malformed_window_is_rejected():
    limits = {"rateLimits": {"limitId": "codex", "primary": {"usedPercent": "25"}}}
    client = CodexAppServerClient(process_factory=FakeProcessFactory(FakeProcess(successful_responses(limits))))
    with pytest.raises(InvalidCodexResponseError, match="usedPercent"):
        await client.read_usage()


@pytest.mark.asyncio
async def test_timeout_is_explicit():
    client = CodexAppServerClient(
        timeout_seconds=0.01,
        process_factory=FakeProcessFactory(FakeProcess([None])),
    )
    with pytest.raises(CodexTimeoutError):
        await client.read_usage()


@pytest.mark.asyncio
async def test_timeout_log_identifies_rpc_without_payload(caplog):
    client = CodexAppServerClient(
        timeout_seconds=0.01,
        process_factory=FakeProcessFactory(FakeProcess([None])),
    )
    with caplog.at_level("INFO", logger="adv_helper.codex_app_server"):
        with pytest.raises(CodexTimeoutError):
            await client.read_usage()
    assert "Codex RPC timed out method=initialize" in caplog.text


@pytest.mark.asyncio
async def test_process_exit_can_restart_on_next_adv_query():
    dead = FakeProcess([])
    live = FakeProcess(successful_responses({"rateLimits": {"limitId": "codex", "primary": None}}))
    factory = FakeProcessFactory(dead, live)
    client = CodexAppServerClient(process_factory=factory)
    with pytest.raises(CodexUnavailableError):
        await client.read_usage()
    snapshot = await client.read_usage()
    assert snapshot.windows == ()
    assert factory.calls == 2


@pytest.mark.asyncio
async def test_close_terminates_idle_process():
    process = FakeProcess(successful_responses({"rateLimits": {"limitId": "codex", "primary": None}}))
    client = CodexAppServerClient(process_factory=FakeProcessFactory(process))
    await client.read_usage()
    await client.close()
    assert process.terminated
