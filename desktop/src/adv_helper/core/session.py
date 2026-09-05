from __future__ import annotations

from dataclasses import dataclass, field
@dataclass
class ConnectionSession:
    connected: bool = False
    pending_exec_ids: set[str] = field(default_factory=set)

    def connect(self) -> None:
        self.disconnect()
        self.connected = True

    def begin_request(self, exec_id: str) -> bool:
        if not self.connected or exec_id in self.pending_exec_ids:
            return False
        self.pending_exec_ids.add(exec_id)
        return True

    def complete_request(self, exec_id: str) -> bool:
        if exec_id not in self.pending_exec_ids:
            return False
        self.pending_exec_ids.remove(exec_id)
        return True

    def disconnect(self) -> None:
        # Session-owned usage must never leak to the next computer or reconnect.
        self.connected = False
        self.pending_exec_ids.clear()
