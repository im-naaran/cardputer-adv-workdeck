from __future__ import annotations

from .messages import RequestMessage, ResponseMessage
from .router import Handler, MessageRouter


class ActionRegistry:
    def __init__(self) -> None:
        self._router = MessageRouter()

    def register(self, action_id: str, handler: Handler) -> None:
        self._router.register(action_id, handler)

    @property
    def capabilities(self) -> tuple[str, ...]:
        return self._router.action_ids

    async def dispatch(self, request: RequestMessage) -> ResponseMessage:
        return await self._router.route(request)
