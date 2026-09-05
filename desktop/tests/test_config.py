import json

import pytest

from adv_helper.config import ConfigError, load_config, parse_config


def valid_action(**overrides):
    action = {
        "type": "codex",
        "actionId": "codex.usage.read",
        "name": "Codex usage",
        "key": None,
        "enabled": True,
        "content": "usage",
        "params": {},
    }
    action.update(overrides)
    return action


def test_defaults_and_file_loading(tmp_path):
    path = tmp_path / "config.json"
    path.write_text(json.dumps({"configVersion": 1, "actions": [valid_action()]}))
    config = load_config(path)
    assert not hasattr(config.codex, "refresh_interval_seconds")
    assert config.codex.request_timeout_seconds == 15
    assert config.warnings == ()
    assert [item.action_id for item in config.enabled_actions] == ["codex.usage.read"]


@pytest.mark.parametrize("value", [60, 3600, 59, 3601, True, "300", None, {}])
def test_legacy_refresh_is_ignored(value):
    config = parse_config({"configVersion": 1, "settings": {"codex": {"refreshIntervalSeconds": value}}})
    assert not hasattr(config.codex, "refresh_interval_seconds")
    assert len(config.warnings) == 1
    assert "ignored" in config.warnings[0]


@pytest.mark.parametrize("value", [5, 15, 60])
def test_rpc_timeout_boundaries(value):
    config = parse_config({"configVersion": 1, "settings": {"codex": {"requestTimeoutSeconds": value}}})
    assert config.codex.request_timeout_seconds == value


@pytest.mark.parametrize("value", [4, 61, True, "15"])
def test_rpc_timeout_invalid(value):
    with pytest.raises(ConfigError):
        parse_config({"configVersion": 1, "settings": {"codex": {"requestTimeoutSeconds": value}}})


def test_unknown_envelope_field_is_strict():
    with pytest.raises(ConfigError, match="unknown fields"):
        parse_config({"configVersion": 1, "future": True})


def test_disabled_action_is_retained_but_not_enabled():
    config = parse_config({"configVersion": 1, "actions": [valid_action(enabled=False)]})
    assert len(config.actions) == 1
    assert config.enabled_actions == ()


def test_duplicate_and_invalid_actions_are_isolated():
    config = parse_config({
        "configVersion": 1,
        "actions": [
            valid_action(),
            valid_action(name="duplicate"),
            valid_action(actionId="future.action"),
        ],
    })
    assert len(config.actions) == 1
    assert len(config.action_errors) == 2
    assert "duplicate actionId" in config.action_errors[0]
    assert "unsupported action" in config.action_errors[1]
