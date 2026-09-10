import json
from pathlib import Path

from adv_helper.core.protocol_constants import (
    ACTION_CODEX_USAGE_READ,
    ACTION_ACTIONS_LIST, ACTION_EXECUTE, ACTION_SHORTCUT_EXECUTE, ACTION_PAGE_SIZE,
    ACTION_SYSTEM_HELLO,
    ACTION_SYSTEM_TIME_READ,
    GATT_ADV_TO_PC_NOTIFY_UUID,
    GATT_PC_TO_ADV_WRITE_UUID,
    GATT_SERVICE_UUID,
    MAX_JSON_BYTES,
    PROTOCOL_VERSION,
    RESULT_CODES,
    SAFE_BLE_CHUNK_BYTES,
)


def test_desktop_constants_match_shared_protocol_fixture():
    path = Path(__file__).parents[2] / "protocol" / "fixtures" / "v2" / "protocol_constants.json"
    fixture = json.loads(path.read_text(encoding="utf-8"))
    assert fixture["protocolVersion"] == PROTOCOL_VERSION
    assert fixture["transport"]["maxJsonBytes"] == MAX_JSON_BYTES
    assert fixture["transport"]["safeChunkBytes"] == SAFE_BLE_CHUNK_BYTES
    assert fixture["actionIds"] == [ACTION_SYSTEM_HELLO, ACTION_CODEX_USAGE_READ, ACTION_SYSTEM_TIME_READ, ACTION_ACTIONS_LIST, ACTION_EXECUTE, ACTION_SHORTCUT_EXECUTE]
    assert fixture["actionPageSize"] == ACTION_PAGE_SIZE
    assert set(fixture["resultCodes"]) == RESULT_CODES
    assert fixture["gatt"] == {
        "serviceUuid": GATT_SERVICE_UUID,
        "advToPcNotifyCharacteristicUuid": GATT_ADV_TO_PC_NOTIFY_UUID,
        "pcToAdvWriteCharacteristicUuid": GATT_PC_TO_ADV_WRITE_UUID,
    }
