import json
from pathlib import Path

import pytest

from adv_helper.application.script_module import ScriptModule
from adv_helper.config import load_config, parse_config
from adv_helper.core.messages import RequestMessage, decode_message, encode_message


def script(index=0, **changes):
    return dict(dict(type="script", actionId=f"script.test.{index}", name=f"脚本 {index}",
                     key="g", enabled=True, content="true", params={}), **changes)


def configured(*actions):
    return parse_config({"configVersion": 1, "actions": list(actions)})


def test_default_and_config_directory(tmp_path):
    config = load_config(Path(__file__).parents[1] / "config.json")
    google = next(a for a in config.actions if a.type == "script")
    assert google.action_id == "script.google.open" and google.key == "g"
    assert google.content == "open https://google.com"
    path = tmp_path / "custom.json"
    path.write_text(json.dumps({"configVersion": 1, "actions": [script(content="\n true \n")]}))
    config = load_config(path)
    assert config.config_directory == tmp_path
    assert config.actions[0].content == "\n true \n"


@pytest.mark.parametrize("change", [
    dict(key=""), dict(key="gg"), dict(key="é"), dict(key="K"), dict(name="x\ny"), dict(name="中" * 22),
    dict(actionId="system.time.read"), dict(actionId="script."), dict(content="a\0b"),
    dict(content="x" * 8193), dict(params={"timeoutSeconds": True}),
    dict(params={"timeoutSeconds": 31}), dict(params={"future": 1}),
])
def test_invalid_optional_script_isolated(change):
    config = configured(script(**change), script(1))
    assert len(config.action_errors) == 1
    assert [a.action_id for a in config.actions] == ["script.test.1"]


@pytest.mark.parametrize("count", [0, 1, 8, 9, 17, 101])
async def test_catalog_has_no_total_count_limit(count):
    config = configured(*(script(i, key="G" if i % 2 else "g") for i in range(count)))
    assert not config.action_errors
    module = ScriptModule(config)
    offset, items = 0, []
    while offset is not None:
        response = await module.list_actions(RequestMessage("request", "actions.list", "x" * 128, {"offset": offset}))
        response = decode_message(encode_message(response)[:-1])
        data = response.result.data
        assert data["total"] == count
        assert len(data["actions"]) <= 8
        items.extend(data["actions"])
        offset = data["nextOffset"]
    assert [item["actionId"] for item in items] == [f"script.test.{i}" for i in range(count)]
    assert [item["effectiveKey"] for item in items] == (["g"] + [None] * (count - 1) if count else [])


async def test_disabled_invalid_and_duplicate_ids_do_not_own_key():
    config = configured(script(enabled=False), script(1, content=""), script(2), script(2), script(3, key=None))
    module = ScriptModule(config)
    page = await module.list_actions(RequestMessage("request", "actions.list", "1", {"offset": 0}))
    assert len(config.action_errors) == 2
    assert [i["actionId"] for i in page.result.data["actions"]] == ["script.test.2", "script.test.3"]
    assert page.result.data["actions"][0]["effectiveKey"] == "g"


@pytest.mark.parametrize("payload", [{}, {"offset": True}, {"offset": -8}, {"offset": 1},
                                     {"offset": 8}, {"offset": 0, "extra": 1}])
async def test_invalid_page_request(payload):
    result = await ScriptModule(configured(script())).list_actions(RequestMessage("request", "actions.list", "1", payload))
    assert result.result.code == "ERROR"


@pytest.mark.parametrize("field", ["name", "content"])
def test_unpaired_unicode_surrogate_is_isolated(field):
    config = configured(script(**{field: "bad\ud800text"}), script(1))
    assert len(config.action_errors) == 1
    assert [action.action_id for action in config.enabled_actions] == ["script.test.1"]
