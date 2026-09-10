import json

import pytest

from adv_helper.config import load_config, parse_config


def item(**changes):
    return dict(dict(type='clipboard', actionId='clipboard.test', name='文本', key='N',
                     enabled=True, content='hello', params={}), **changes)


def config(*items):
    return parse_config(dict(configVersion=1, actions=list(items)))


@pytest.mark.parametrize('text', [' ', '\n', '\t\r\n', '  中文🙂\r\n\ttext  ', 'x'*8192,
                                 '中'*2730+'ab', '{\\rtf1 literal}', '%!PS-Adobe literal'])
def test_preserves_exact_text_on_load(tmp_path, text):
    path = tmp_path / 'config.json'
    path.write_text(json.dumps(dict(configVersion=1, actions=[item(content=text)])))
    loaded = load_config(path)
    assert not loaded.action_errors
    assert loaded.actions[0].content == text
    assert loaded.actions[0].key == 'n'


@pytest.mark.parametrize('change', [dict(content=''), dict(content='x'*8193), dict(content='中'*2731),
    dict(content='bad\ud800text'), dict(content='bad\0text'), dict(content=None), dict(content=12),
    dict(key='K'), dict(key='nn'), dict(actionId='clipboard.'), dict(actionId='script.test'),
    dict(name='x\ny'), dict(name='中'*22), dict(params={'timeoutSeconds': 2}), dict(enabled=1)])
def test_bad_clipboard_does_not_break_other_actions(change):
    loaded = config(item(**change), item(actionId='clipboard.good'))
    assert len(loaded.action_errors) == 1
    assert [a.action_id for a in loaded.actions] == ['clipboard.good']


def test_no_business_count_limit_and_mixed_duplicate_keys():
    entries = [item(actionId=f'clipboard.test.{i}') for i in range(101)]
    entries.insert(0, item(type='script', actionId='script.first', content='true'))
    loaded = config(*entries)
    assert not loaded.action_errors
    assert len(loaded.actions) == 102
    assert {a.key for a in loaded.actions} == {'n'}
    # Shortcut ownership is assigned by task-05; parsing must preserve full ordering.
    assert loaded.actions[0].action_id == 'script.first'


def test_duplicate_id_disabled_and_unknown_keys_do_not_leak_text():
    secret = 'private-snippet-body'
    bad = item(params={secret: secret})
    loaded = config(bad, item(**{secret: secret}), item(enabled=False), item())
    assert len(loaded.action_errors) == 3
    assert not loaded.enabled_actions
    assert secret not in repr(loaded.action_errors)


def test_reload_reads_latest_content(tmp_path):
    path = tmp_path / 'config.json'
    for text in ['first', ' next\n']:
        path.write_text(json.dumps(dict(configVersion=1, actions=[item(content=text)])))
        assert load_config(path).actions[0].content == text
