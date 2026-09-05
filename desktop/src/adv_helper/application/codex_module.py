from __future__ import annotations

from adv_helper.core.messages import (
    ACTION_CODEX_USAGE_READ,
    RequestMessage,
    ResponseMessage,
    Result,
    error_response,
)
from adv_helper.os_adapters.codex_app_server import (
    CodexProviderError,
    CodexTimeoutError,
    CodexUnavailableError,
    InvalidCodexResponseError,
    NotLoggedInError,
    RateLimitProvider,
)


class QueryGate:
    def __init__(self) -> None:
        self._busy = False

    def try_enter(self) -> bool:
        # ADV is the real scheduler; this gate only rejects unexpected PC-side concurrency.
        if self._busy:
            return False
        self._busy = True
        return True

    def leave(self) -> None:
        self._busy = False


class CodexModule:
    def __init__(self, provider: RateLimitProvider) -> None:
        self._provider = provider
        self._gate = QueryGate()

    async def handle(self, request: RequestMessage) -> ResponseMessage:
        # No periodic task exists here: every provider call starts with an ADV request.
        if request.action_id != ACTION_CODEX_USAGE_READ:
            return error_response(request, "ERROR", "unsupported Codex action")
        if not self._gate.try_enter():
            return error_response(request, "BUSY", "Codex usage query already running")
        try:
            snapshot = await self._provider.read_usage()
            return ResponseMessage(
                "response",
                request.action_id,
                request.exec_id,
                Result("OK", "usage loaded", snapshot.to_protocol_dict()),
            )
        except NotLoggedInError:
            return error_response(request, "NOT_LOGGED_IN", "Codex login required")
        except CodexTimeoutError:
            return error_response(request, "TIMEOUT", "Codex App Server request timed out")
        except CodexUnavailableError:
            return error_response(request, "CODEX_UNAVAILABLE", "Codex App Server unavailable")
        except InvalidCodexResponseError:
            return error_response(request, "INVALID_RESPONSE", "Codex App Server returned invalid data")
        except CodexProviderError:
            return error_response(request, "ERROR", "Codex usage query failed")
        except Exception:
            return error_response(request, "ERROR", "unexpected Codex usage error")
        finally:
            self._gate.leave()
