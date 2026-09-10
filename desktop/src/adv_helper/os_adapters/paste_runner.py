"""Platform-neutral paste boundary; macOS uses fixed programs and stdin-only text."""
from __future__ import annotations

import asyncio
import os
import platform
import re
from dataclasses import dataclass
from typing import Protocol

from adv_helper.core.action_contract import utf8_fits
from .process_runner import ProcessRunner, ProcessResult

# Keep these programs in the Python package: no resource lookup or text interpolation.
WRITE_PROGRAM = r'''
ObjC.import('Foundation');
ObjC.import('AppKit');
function run() {
    try {
        var data = $.NSFileHandle.fileHandleWithStandardInput.readDataToEndOfFile;
        var text = $.NSString.alloc.initWithDataEncoding(data, $.NSUTF8StringEncoding);
        if (text.isNil()) return 'FAILED';
        var board = $.NSPasteboard.generalPasteboard;
        board.clearContents;
        return board.setStringForType(text, $.NSPasteboardTypeString) ? 'OK' : 'FAILED';
    } catch (error) {
        return 'ERROR:' + (Number.isInteger(error.errorNumber) ? error.errorNumber : 0);
    }
}
'''
PASTE_PROGRAM = '''
try
    tell application "System Events" to keystroke "v" using command down
    return "OK"
on error ignoredMessage number errorNumber
    return "ERROR:" & errorNumber
end try
'''


@dataclass(frozen=True)
class PasteResult:
    code: str
    reason: str | None
    clipboard_written: bool | None
    paste_sent: bool | None


class PasteRunner(Protocol):
    @property
    def available(self) -> bool: ...

    async def paste(self, text: str) -> PasteResult: ...


class UnavailablePasteRunner:
    available = False

    async def paste(self, text: str) -> PasteResult:
        return PasteResult('ERROR', 'UNAVAILABLE', False, False)


class MacOSPasteRunner:
    def __init__(self, process_runner: ProcessRunner | None = None, *, timeout_seconds: float = 10,
                 write_settle_seconds: float = 0.05, paste_settle_seconds: float = 0.15) -> None:
        self._process = process_runner or ProcessRunner()
        self._timeout = timeout_seconds
        self._write_settle = write_settle_seconds
        self._paste_settle = paste_settle_seconds

    @property
    def available(self) -> bool:
        return self._process.available

    async def paste(self, text: str) -> PasteResult:
        if not self.available:
            return await UnavailablePasteRunner().paste(text)
        if not isinstance(text, str) or not text or '\0' in text or not utf8_fits(text, 8192):
            return PasteResult('ERROR', 'WRITE_FAILED', False, False)
        written: bool | None = None
        sent: bool | None = False
        deadline = asyncio.get_running_loop().time() + self._timeout
        try:
            # One deadline covers creation, both OS calls and settling. ProcessRunner
            # retains ownership until cancellation cleanup completes, even past this budget.
            async with asyncio.timeout_at(deadline):
                result = await self._process.execute(
                    ('/usr/bin/osascript', '-l', 'JavaScript', '-e', WRITE_PROGRAM),
                    input_data=text.encode('utf-8'), output_limit=64,
                    timeout_seconds=max(0, deadline - asyncio.get_running_loop().time()),
                )
                if result.code != 'OK' or result.output.strip() != b'OK':
                    return self._failure(result, writing=True, written=None)
                written = True
                await asyncio.sleep(self._write_settle)
                sent = None  # Once invoked, lack of a result cannot mean "not sent".
                result = await self._process.execute(
                    ('/usr/bin/osascript', '-e', PASTE_PROGRAM), output_limit=64,
                    timeout_seconds=max(0, deadline - asyncio.get_running_loop().time()),
                )
                if result.code != 'OK' or result.output.strip() != b'OK':
                    return self._failure(result, writing=False, written=True)
                sent = True
                # Avoid immediately replacing text before a typical target consumes it;
                # this delay does not establish that the target actually inserted text.
                await asyncio.sleep(self._paste_settle)
                return PasteResult('OK', None, True, True)
        except TimeoutError:
            return PasteResult('TIMEOUT', 'UNCONFIRMED', written, sent)
        # Session cancellation deliberately propagates; never continue to the next stage.

    @staticmethod
    def _failure(result: ProcessResult, *, writing: bool, written: bool | None) -> PasteResult:
        status = result.output.strip()
        if result.code == 'TIMEOUT' or (result.code == 'OK' and status == b'ERROR:-1712'):
            return PasteResult('TIMEOUT', 'UNCONFIRMED', written, False if writing else None)
        # Only fixed numeric errors are classified. Never parse localized stderr.
        denied = status in (b'ERROR:-1743', b'ERROR:-25211') and result.code == 'OK'
        confirmed_failure = result.code == 'OK' and (status == b'FAILED' or re.fullmatch(rb'ERROR:-?[0-9]+', status) is not None)
        if not confirmed_failure:
            reason = 'UNCONFIRMED'
        elif denied:
            reason = 'PERMISSION_DENIED'
        else:
            reason = 'WRITE_FAILED' if writing else 'PASTE_FAILED'
        return PasteResult('ERROR', reason,
                           False if writing and confirmed_failure else written,
                           False if writing or confirmed_failure else None)


def create_paste_runner() -> PasteRunner:
    # Later Windows/Linux implementations replace only this selection boundary.
    if platform.system() == 'Darwin' and os.access('/usr/bin/osascript', os.X_OK):
        return MacOSPasteRunner()
    return UnavailablePasteRunner()
