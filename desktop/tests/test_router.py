import pytest

from adv_helper.core.messages import RequestMessage, ResponseMessage, Result
from adv_helper.core.router import MessageRouter


@pytest.mark.asyncio
async def test_route_and_unknown_action():
    router = MessageRouter()

    async def handler(request):
        return ResponseMessage("response", request.action_id, request.exec_id, Result("OK", "done", {}))

    router.register("known", handler)
    response = await router.route(RequestMessage("request", "known", "1", {}))
    assert response.result.code == "OK"
    unknown = await router.route(RequestMessage("request", "unknown", "2", {}))
    assert unknown.result.code == "ERROR"


@pytest.mark.asyncio
async def test_handler_failure_is_isolated():
    router = MessageRouter()

    async def broken(_request):
        raise RuntimeError("boom")

    router.register("broken", broken)
    response = await router.route(RequestMessage("request", "broken", "1", {}))
    assert response.result.code == "ERROR"
