from __future__ import annotations

from collections.abc import Awaitable, Callable

from .messages import RequestMessage, ResponseMessage, error_response


Handler = Callable[[RequestMessage], Awaitable[ResponseMessage]]


class MessageRouter:
    def __init__(self) -> None:
        self._handlers: dict[str, Handler] = {}

    def register(self, action_id: str, handler: Handler) -> None:
        if action_id in self._handlers:
            raise ValueError(f"handler already registered for {action_id}")
        self._handlers[action_id] = handler

    @property
    def action_ids(self) -> tuple[str, ...]:
        return tuple(sorted(self._handlers))

    async def route(self, request: RequestMessage) -> ResponseMessage:
        handler = self._handlers.get(request.action_id)
        if handler is None:
            return error_response(request, "ERROR", f"unknown action: {request.action_id}")
        try:
            response = await handler(request)
        except Exception:
            # A business module failure must not terminate BLE diagnostics or other modules.
            return error_response(request, "ERROR", "action handler failed")
        if response.exec_id != request.exec_id or response.action_id != request.action_id:
            return error_response(request, "ERROR", "handler returned mismatched response")
        return response
