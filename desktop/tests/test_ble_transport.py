import asyncio
from dataclasses import dataclass

import pytest

from adv_helper.platform.ble_transport import (
    ADV_TO_PC_NOTIFY_UUID,
    PC_TO_ADV_WRITE_UUID,
    SERVICE_UUID,
    BleTransport,
    BleTransportConfig,
    BleTransportError,
    reconnect_delay,
)


@dataclass
class Device:
    name: str
    address: str
    metadata: dict | None = None


@dataclass
class Advertisement:
    service_uuids: list[str]


class Scanner:
    def __init__(self, responses):
        self.responses = list(responses)
        self.calls = []

    async def discover(self, **kwargs):
        self.calls.append(kwargs)
        value = self.responses.pop(0) if self.responses else {}
        if isinstance(value, Exception):
            raise value
        return value


class Services:
    def __init__(self, missing=None):
        self.missing = missing

    def get_characteristic(self, uuid):
        return None if uuid == self.missing else object()


class Client:
    def __init__(self, device, disconnected_callback, *, missing=None, write_error=None):
        self.device = device
        self.disconnected_callback = disconnected_callback
        self.services = Services(missing)
        self.is_connected = False
        self.notify_callback = None
        self.writes = []
        self.write_error = write_error

    async def connect(self):
        self.is_connected = True

    async def start_notify(self, uuid, callback):
        assert uuid == ADV_TO_PC_NOTIFY_UUID
        self.notify_callback = callback

    async def write_gatt_char(self, uuid, data, response):
        if self.write_error:
            raise self.write_error
        self.writes.append((uuid, bytes(data), response))

    async def disconnect(self):
        self.is_connected = False


def advertised(device, services):
    return {device.address: (device, Advertisement(services))}


@pytest.mark.asyncio
async def test_service_scan_falls_back_to_unfiltered_scan():
    device = Device("Cardputer-Adv", "corebluetooth-id")
    scanner = Scanner([{}, advertised(device, [])])
    transport = BleTransport(scanner=scanner, client_factory=Client)
    assert await transport.scan() == [device]
    assert scanner.calls[0]["service_uuids"] == [SERVICE_UUID]
    assert "service_uuids" not in scanner.calls[1]


@pytest.mark.asyncio
async def test_multiple_devices_require_selector():
    one = Device("Cardputer-Adv", "one")
    two = Device("Cardputer-Adv", "two")
    scanner = Scanner([{
        "one": (one, Advertisement([SERVICE_UUID])),
        "two": (two, Advertisement([SERVICE_UUID])),
    }])
    transport = BleTransport(scanner=scanner, client_factory=Client)
    with pytest.raises(BleTransportError, match="multiple"):
        await transport.select_device()


@pytest.mark.asyncio
async def test_device_id_selects_corebluetooth_uuid():
    one = Device("Cardputer-Adv", "one")
    two = Device("Cardputer-Adv", "two")
    scanner = Scanner([{
        "one": (one, Advertisement([])),
        "two": (two, Advertisement([])),
    }])
    config = BleTransportConfig(device_id="two")
    transport = BleTransport(config, scanner=scanner, client_factory=Client)
    assert await transport.select_device() == two


@pytest.mark.asyncio
async def test_missing_characteristic_is_reported_after_finite_retries():
    device = Device("Cardputer-Adv", "one")
    scanner = Scanner([advertised(device, [SERVICE_UUID])] * 2)
    clients = []

    def factory(device, disconnected_callback):
        client = Client(device, disconnected_callback, missing=PC_TO_ADV_WRITE_UUID)
        clients.append(client)
        return client

    config = BleTransportConfig(discovery_retries=2, connect_delay_seconds=0, discovery_retry_delay_seconds=0)
    transport = BleTransport(config, scanner=scanner, client_factory=factory)
    with pytest.raises(BleTransportError, match="write characteristic"):
        await transport.connect()
    assert len(clients) == 2


@pytest.mark.asyncio
async def test_notify_bytes_and_twenty_byte_writes():
    device = Device("Cardputer-Adv", "one")
    scanner = Scanner([advertised(device, [SERVICE_UUID])])
    clients = []

    def factory(device, disconnected_callback):
        client = Client(device, disconnected_callback)
        clients.append(client)
        return client

    config = BleTransportConfig(connect_delay_seconds=0)
    transport = BleTransport(config, scanner=scanner, client_factory=factory)
    await transport.connect()
    client = clients[0]
    client.notify_callback(None, bytearray("中文".encode()))
    assert await transport.receive_chunk() == "中文".encode()
    await transport.send(b"x" * 41)
    assert [len(item[1]) for item in client.writes] == [20, 20, 1]
    assert all(item[0] == PC_TO_ADV_WRITE_UUID and item[2] is True for item in client.writes)


@pytest.mark.asyncio
async def test_disconnect_callback_ends_receive():
    device = Device("Cardputer-Adv", "one")
    scanner = Scanner([advertised(device, [SERVICE_UUID])])
    clients = []

    def factory(device, disconnected_callback):
        client = Client(device, disconnected_callback)
        clients.append(client)
        return client

    transport = BleTransport(BleTransportConfig(connect_delay_seconds=0), scanner=scanner, client_factory=factory)
    await transport.connect()
    clients[0].disconnected_callback(clients[0])
    with pytest.raises(ConnectionError):
        await transport.receive_chunk()


@pytest.mark.asyncio
async def test_write_error_becomes_transport_error_and_ends_receive():
    device = Device("Cardputer-Adv", "one")
    scanner = Scanner([advertised(device, [SERVICE_UUID])])
    clients = []

    def factory(device, disconnected_callback):
        client = Client(device, disconnected_callback, write_error=RuntimeError("link lost"))
        clients.append(client)
        return client

    transport = BleTransport(BleTransportConfig(connect_delay_seconds=0), scanner=scanner, client_factory=factory)
    await transport.connect()
    with pytest.raises(BleTransportError, match="link lost"):
        await transport.send(b"payload")
    with pytest.raises(ConnectionError):
        await transport.receive_chunk()


@pytest.mark.asyncio
async def test_write_failure_closes_old_link_before_reconnect():
    device = Device("Cardputer-Adv", "one")
    scanner = Scanner([advertised(device, [SERVICE_UUID])] * 2)
    clients = []

    def factory(device, disconnected_callback):
        # Reconnecting must not leave a previous physical link open.
        assert all(not client.is_connected for client in clients)
        error = RuntimeError("write failed") if not clients else None
        client = Client(device, disconnected_callback, write_error=error)
        clients.append(client)
        return client

    transport = BleTransport(BleTransportConfig(connect_delay_seconds=0), scanner=scanner, client_factory=factory)
    await transport.connect()
    with pytest.raises(BleTransportError, match="write failed"):
        await transport.send(b"first\n")
    with pytest.raises(ConnectionError):
        await transport.receive_chunk()
    await transport.disconnect()
    assert not clients[0].is_connected

    await transport.connect()
    await transport.send(b"second\n")
    assert [write[1] for write in clients[1].writes] == [b"second\n"]
    await transport.disconnect()


def test_notify_queue_overflow_is_reported():
    errors = []
    transport = BleTransport(on_error=errors.append)
    for _ in range(257):
        transport._on_notify(None, bytearray(b"x"))
    assert errors == ["BLE notify queue full; dropped incoming chunk"]


def test_reconnect_backoff_is_capped():
    config = BleTransportConfig(reconnect_initial_seconds=1, reconnect_max_seconds=8)
    assert [reconnect_delay(attempt, config) for attempt in range(1, 7)] == [1, 2, 4, 8, 8, 8]


@pytest.mark.asyncio
async def test_reconnect_reports_connection_error():
    errors = []

    class FailingTransport(BleTransport):
        async def connect(self):
            raise BleTransportError("permission denied")

        async def disconnect(self):
            return None

    transport = FailingTransport(
        BleTransportConfig(reconnect_initial_seconds=10),
        on_error=errors.append,
    )
    stop = asyncio.Event()
    task = asyncio.create_task(transport.run(lambda _: asyncio.sleep(0), stop))
    while not errors:
        await asyncio.sleep(0)
    stop.set()
    await task
    assert errors == ["permission denied"]


@pytest.mark.asyncio
async def test_stop_event_cancels_blocked_connected_session():
    class RunningTransport(BleTransport):
        def __init__(self):
            super().__init__()
            self.connected = asyncio.Event()
            self.disconnected = False

        async def connect(self):
            self.connected.set()

        async def disconnect(self):
            self.disconnected = True

    async def blocked_session(_transport):
        await asyncio.Event().wait()

    transport = RunningTransport()
    stop = asyncio.Event()
    task = asyncio.create_task(transport.run(blocked_session, stop))
    await transport.connected.wait()
    stop.set()
    await asyncio.wait_for(task, 0.1)
    assert transport.disconnected
