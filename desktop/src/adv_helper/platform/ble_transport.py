from __future__ import annotations

import asyncio
import platform
from collections.abc import Awaitable, Callable
from dataclasses import dataclass
from typing import Any

from adv_helper.core.protocol_constants import (
    GATT_ADV_TO_PC_NOTIFY_UUID as ADV_TO_PC_NOTIFY_UUID,
    GATT_PC_TO_ADV_WRITE_UUID as PC_TO_ADV_WRITE_UUID,
    GATT_SERVICE_UUID as SERVICE_UUID,
    SAFE_BLE_CHUNK_BYTES,
)



class BleTransportError(RuntimeError):
    pass


class BlePermissionError(BleTransportError):
    pass


@dataclass(frozen=True)
class BleTransportConfig:
    device_name: str = ""
    device_id: str = ""
    default_device_name: str = "Cardputer-Adv"
    scan_timeout_seconds: float = 8
    connect_delay_seconds: float = 1
    discovery_timeout_seconds: float = 5
    discovery_retries: int = 3
    discovery_retry_delay_seconds: float = 0.5
    write_chunk_bytes: int = SAFE_BLE_CHUNK_BYTES
    reconnect_initial_seconds: float = 1
    reconnect_max_seconds: float = 30


class BleTransport:
    def __init__(
        self,
        config: BleTransportConfig = BleTransportConfig(),
        *,
        scanner: Any = None,
        client_factory: Callable[..., Any] | None = None,
        sleep: Callable[[float], Awaitable[None]] = asyncio.sleep,
        on_error: Callable[[str], None] | None = None,
    ) -> None:
        self.config = config
        self._scanner = scanner
        self._client_factory = client_factory
        self._sleep = sleep
        self._on_error = on_error
        self._client: Any = None
        self._chunks: asyncio.Queue[bytes | None] = asyncio.Queue(maxsize=256)
        self._intentional_disconnect = False

    async def _load_bleak(self) -> tuple[Any, Callable[..., Any]]:
        ensure_macos_bluetooth_usage_description()
        if self._scanner is not None and self._client_factory is not None:
            return self._scanner, self._client_factory
        from bleak import BleakClient, BleakScanner

        return self._scanner or BleakScanner, self._client_factory or BleakClient

    async def scan(self) -> list[Any]:
        scanner, _ = await self._load_bleak()
        try:
            found = await self._discover(scanner, [SERVICE_UUID])
            # Some CoreBluetooth advertisements omit service UUIDs, so retry unfiltered.
            if not found:
                found = await self._discover(scanner, None)
        except Exception as error:
            raise _friendly_ble_error(error) from error
        return self._matching_devices(found)

    async def _discover(self, scanner: Any, service_uuids: list[str] | None) -> list[tuple[Any, list[str]]]:
        kwargs: dict[str, Any] = {
            "timeout": self.config.scan_timeout_seconds,
            "return_adv": True,
        }
        if service_uuids is not None:
            kwargs["service_uuids"] = service_uuids
        try:
            discovered = await scanner.discover(**kwargs)
            if isinstance(discovered, dict):
                return [
                    (device, list(getattr(advertisement, "service_uuids", None) or []))
                    for device, advertisement in discovered.values()
                ]
        except TypeError:
            kwargs.pop("return_adv")
            discovered = await scanner.discover(**kwargs)
        return [(device, _device_service_uuids(device)) for device in discovered]

    def _matching_devices(self, discovered: list[tuple[Any, list[str]]]) -> list[Any]:
        matches: list[Any] = []
        seen: set[str] = set()
        for device, advertised_services in discovered:
            # On macOS address is a CoreBluetooth UUID, not a public hardware MAC.
            device_id = str(getattr(device, "address", "") or "")
            name = str(getattr(device, "name", "") or "")
            if self.config.device_id and _normalize_id(device_id) != _normalize_id(self.config.device_id):
                continue
            if self.config.device_name and name != self.config.device_name:
                continue
            if not self.config.device_id and not self.config.device_name:
                has_service = _normalize_uuid(SERVICE_UUID) in {
                    _normalize_uuid(item) for item in advertised_services
                }
                if not has_service and name != self.config.default_device_name:
                    continue
            key = _normalize_id(device_id or name)
            if key and key not in seen:
                matches.append(device)
                seen.add(key)
        return matches

    async def select_device(self) -> Any:
        devices = await self.scan()
        if not devices:
            raise BleTransportError("Cardputer-Adv not found; confirm it is powered on and advertising")
        if len(devices) > 1:
            labels = ", ".join(describe_device(device) for device in devices)
            raise BleTransportError(f"multiple ADV devices found: {labels}; use --ble-id or --ble-name")
        return devices[0]

    async def connect(self) -> None:
        _, client_factory = await self._load_bleak()
        last_error: Exception | None = None
        while not self._chunks.empty():
            self._chunks.get_nowait()
        for attempt in range(self.config.discovery_retries):
            device = await self.select_device()
            client = client_factory(device, disconnected_callback=self._on_disconnected)
            try:
                await client.connect()
                if self.config.connect_delay_seconds:
                    await self._sleep(self.config.connect_delay_seconds)
                await asyncio.wait_for(
                    self._validate_characteristics(client),
                    timeout=self.config.discovery_timeout_seconds,
                )
                await client.start_notify(ADV_TO_PC_NOTIFY_UUID, self._on_notify)
                self._client = client
                self._intentional_disconnect = False
                return
            except Exception as error:
                last_error = error
                if getattr(client, "is_connected", False):
                    self._intentional_disconnect = True
                    try:
                        await client.disconnect()
                    except Exception as disconnect_error:
                        self._report_error(f"BLE cleanup failed: {disconnect_error}")
                    self._intentional_disconnect = False
                if attempt + 1 < self.config.discovery_retries:
                    await self._sleep(self.config.discovery_retry_delay_seconds)
        raise _friendly_ble_error(last_error or BleTransportError("BLE connection failed"))

    async def _validate_characteristics(self, client: Any) -> None:
        get_services = getattr(client, "get_services", None)
        services = await get_services() if callable(get_services) else getattr(client, "services", None)
        if services is None:
            raise BleTransportError("GATT services are unavailable")
        if services.get_characteristic(ADV_TO_PC_NOTIFY_UUID) is None:
            raise BleTransportError("ADV notify characteristic is missing")
        if services.get_characteristic(PC_TO_ADV_WRITE_UUID) is None:
            raise BleTransportError("ADV write characteristic is missing")

    def _on_notify(self, _sender: Any, data: bytearray) -> None:
        try:
            self._chunks.put_nowait(bytes(data))
        except asyncio.QueueFull:
            self._report_error("BLE notify queue full; dropped incoming chunk")

    def _on_disconnected(self, client: Any) -> None:
        if self._client is not None and client is not self._client:
            return
        self._client = None
        if not self._intentional_disconnect:
            self._signal_disconnected()

    def _signal_disconnected(self) -> None:
        while not self._chunks.empty():
            self._chunks.get_nowait()
        self._chunks.put_nowait(None)

    async def receive_chunk(self) -> bytes:
        chunk = await self._chunks.get()
        if chunk is None:
            raise ConnectionError("BLE device disconnected")
        return chunk

    async def send(self, payload: bytes) -> None:
        client = self._client
        if client is None or not getattr(client, "is_connected", False):
            raise ConnectionError("BLE device is not connected")
        size = self.config.write_chunk_bytes
        try:
            for offset in range(0, len(payload), size):
                await client.write_gatt_char(
                    PC_TO_ADV_WRITE_UUID,
                    payload[offset : offset + size],
                    response=True,
                )
        except Exception as error:
            # A failed write need not close the BLE link. Keep the client so
            # run() can disconnect it before attempting a new connection.
            self._signal_disconnected()
            raise _friendly_ble_error(error) from error

    async def disconnect(self) -> None:
        client = self._client
        self._client = None
        self._intentional_disconnect = True
        if client is not None and getattr(client, "is_connected", False):
            try:
                await client.disconnect()
            except Exception as error:
                self._report_error(f"BLE disconnect failed: {error}")

    async def run(
        self,
        session: Callable[["BleTransport"], Awaitable[None]],
        stop_event: asyncio.Event,
    ) -> None:
        attempt = 0
        while not stop_event.is_set():
            try:
                await self.connect()
                attempt = 0
                session_task = asyncio.create_task(session(self))
                stop_task = asyncio.create_task(stop_event.wait())
                done, pending = await asyncio.wait(
                    {session_task, stop_task},
                    return_when=asyncio.FIRST_COMPLETED,
                )
                for task in pending:
                    task.cancel()
                await asyncio.gather(*pending, return_exceptions=True)
                if session_task in done:
                    await session_task
            except (BleTransportError, ConnectionError) as error:
                attempt += 1
                if self._on_error is not None:
                    self._on_error(str(error))
            finally:
                await self.disconnect()
            if not stop_event.is_set():
                delay = reconnect_delay(attempt, self.config)
                try:
                    await asyncio.wait_for(stop_event.wait(), delay)
                except TimeoutError:
                    pass

    def _report_error(self, message: str) -> None:
        if self._on_error is None:
            return
        try:
            self._on_error(message)
        except Exception:
            return


def ensure_macos_bluetooth_usage_description() -> None:
    if platform.system() != "Darwin":
        return
    try:
        from Foundation import NSBundle

        info = NSBundle.mainBundle().infoDictionary()
        if info is not None:
            purpose = "Cardputer ADV Workdeck uses Bluetooth to exchange local workstation status."
            info["NSBluetoothAlwaysUsageDescription"] = purpose
            info["NSBluetoothPeripheralUsageDescription"] = purpose
    except Exception:
        return


def _friendly_ble_error(error: Exception) -> BleTransportError:
    message = str(error)
    lowered = message.lower()
    if "not authorized" in lowered or "permission" in lowered:
        return BlePermissionError(
            "Bluetooth permission denied; enable it for the terminal or app in macOS System Settings > Privacy & Security > Bluetooth"
        )
    if isinstance(error, BleTransportError):
        return error
    return BleTransportError(message or "BLE operation failed")


def _device_service_uuids(device: Any) -> list[str]:
    metadata = getattr(device, "metadata", {}) or {}
    return list(metadata.get("uuids", []) or [])


def _normalize_uuid(value: Any) -> str:
    return str(value or "").replace("-", "").lower()


def _normalize_id(value: Any) -> str:
    return str(value or "").replace(":", "").lower()


def describe_device(device: Any) -> str:
    return f"{getattr(device, 'name', None) or 'unnamed'} ({getattr(device, 'address', None) or 'unknown'})"


def reconnect_delay(attempt: int, config: BleTransportConfig) -> float:
    return min(
        config.reconnect_initial_seconds * (2 ** max(0, attempt - 1)),
        config.reconnect_max_seconds,
    )
