import asyncio
import signal
from argparse import Namespace
from unittest.mock import AsyncMock, Mock

import pytest

from adv_helper import main


@pytest.mark.asyncio
async def test_signals_stop_then_force_exit_and_restore_handlers(monkeypatch):
    loop = asyncio.get_running_loop()
    handlers = {}
    removed = []
    restored = []
    previous = {sig: signal.getsignal(sig) for sig in (signal.SIGINT, signal.SIGTERM)}
    monkeypatch.setattr(loop, 'add_signal_handler', lambda sig, callback, *args: handlers.update({sig: (callback, args)}))
    monkeypatch.setattr(loop, 'remove_signal_handler', removed.append)
    monkeypatch.setattr(signal, 'signal', lambda sig, handler: restored.append((sig, handler)))
    force_exit = Mock(side_effect=SystemExit(130))
    monkeypatch.setattr(main.os, '_exit', force_exit)
    monkeypatch.setattr(main, 'load_config', lambda _: object())
    diagnostics = Mock()
    monkeypatch.setattr(main, 'configure_logging', lambda _: diagnostics)
    application = Mock(close=AsyncMock())
    monkeypatch.setattr(main, 'build_application', lambda *_: application)

    async def run_session(_, stop):
        callback, args = handlers[signal.SIGINT]
        callback(*args)
        assert stop.is_set()
        force_exit.assert_not_called()

    async def disconnect():
        # The second interrupt must remain active throughout shutdown cleanup.
        callback, args = handlers[signal.SIGINT]
        with pytest.raises(SystemExit) as error:
            callback(*args)
        assert error.value.code == 130
        force_exit.assert_called_once_with(130)

    transport = Mock(run=AsyncMock(side_effect=run_session), disconnect=AsyncMock(side_effect=disconnect))
    monkeypatch.setattr(main, 'build_ble_transport', lambda **_: transport)
    args = Namespace(verbose=False, config=None, ble_name='', ble_id='', list_ble=False)
    assert await main.run(args) == 0
    application.close.assert_awaited_once()
    assert set(removed) == set(previous)
    assert dict(restored) == previous
    diagnostics.info.assert_called_once()
