from __future__ import annotations

import hashlib
import platform
import re
import subprocess
import uuid
from dataclasses import dataclass


@dataclass(frozen=True)
class ComputerIdentity:
    stable_id: str
    display_name: str


def load_computer_identity() -> ComputerIdentity:
    display_name = platform.node() or "Mac"
    source = _macos_platform_uuid()
    if source is None:
        # getnode is a stable fallback on ordinary desktops; hash it before BLE exposure.
        source = f"{platform.system()}:{uuid.getnode():012x}"
    digest = hashlib.sha256(f"cardputer-adv-workdeck:{source}".encode()).hexdigest()[:32]
    return ComputerIdentity(digest, display_name)


def _macos_platform_uuid() -> str | None:
    if platform.system() != "Darwin":
        return None
    try:
        result = subprocess.run(
            ["ioreg", "-rd1", "-c", "IOPlatformExpertDevice"],
            capture_output=True,
            check=False,
            text=True,
            timeout=2,
        )
    except (OSError, subprocess.SubprocessError):
        return None
    if result.returncode != 0:
        return None
    match = re.search(r'"IOPlatformUUID"\s*=\s*"([^"]+)"', result.stdout)
    return match.group(1) if match else None
