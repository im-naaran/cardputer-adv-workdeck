from __future__ import annotations

import argparse
import asyncio
import signal
import sys
from pathlib import Path

from adv_helper.bootstrap import build_application, build_ble_transport
from adv_helper.config import ConfigError, load_config
from adv_helper.platform.ble_transport import BleTransportError, describe_device
from adv_helper.platform.diagnostics import configure_logging


DESKTOP_DIR = Path(__file__).resolve().parents[2]
DEFAULT_CONFIG_PATH = DESKTOP_DIR / "config.json"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Cardputer ADV Workdeck macOS companion service")
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG_PATH, help="JSON config path")
    parser.add_argument("--ble-name", default="", help="select an ADV by exact advertised name")
    parser.add_argument("--ble-id", default="", help="select an ADV by CoreBluetooth device UUID")
    parser.add_argument("--list-ble", action="store_true", help="list matching ADV devices and exit")
    parser.add_argument("--verbose", action="store_true", help="enable local debug diagnostics")
    return parser


async def run(args: argparse.Namespace) -> int:
    diagnostics = configure_logging(args.verbose)
    try:
        config = load_config(args.config)
    except ConfigError as error:
        diagnostics.error("configuration error", detail=str(error), path=str(args.config))
        return 2

    transport = build_ble_transport(
        device_name=args.ble_name,
        device_id=args.ble_id,
        on_error=lambda detail: diagnostics.error("BLE session failed", detail=detail),
    )
    if args.list_ble:
        try:
            devices = await transport.scan()
        except BleTransportError as error:
            diagnostics.error("BLE scan failed", detail=str(error))
            return 3
        for device in devices:
            print(describe_device(device))
        return 0

    application = build_application(config, diagnostics)
    stop_event = asyncio.Event()
    loop = asyncio.get_running_loop()
    for name in (signal.SIGINT, signal.SIGTERM):
        try:
            loop.add_signal_handler(name, stop_event.set)
        except NotImplementedError:
            pass
    try:
        await transport.run(application.run_session, stop_event)
    finally:
        await transport.disconnect()
        await application.close()
    return 0


def cli() -> None:
    # Python 3.14/CoreBluetooth is outside the validated Bleak runtime boundary.
    if sys.version_info < (3, 12) or sys.version_info >= (3, 14):
        print("Python 3.12 or 3.13 is required; run this command through uv.", file=sys.stderr)
        raise SystemExit(1)
    try:
        raise SystemExit(asyncio.run(run(build_parser().parse_args())))
    except KeyboardInterrupt:
        raise SystemExit(130) from None
