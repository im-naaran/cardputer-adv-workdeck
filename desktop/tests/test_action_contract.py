import copy
import json
from pathlib import Path

import pytest

from adv_helper.core import action_contract as contract
from adv_helper.core.messages import parse_message, encode_message, decode_message

FIXTURES = Path(__file__).parents[2] / 'protocol/fixtures/v2'


def load(name):
    return json.loads((FIXTURES / (name + '.json')).read_text())


def validate(message):
    if message['event'] == 'request':
        contract.validate_request(message['actionId'], message['payload'])
    else:
        result = message['result']
        if message['actionId'] == 'system.hello':
            assert result['data']['protocolVersion'] == 2
            contract.validate_supported_types(result['data']['supportedActionTypes'])
        elif message['actionId'] == 'actions.list' and result['code'] == 'OK':
            contract.validate_page(result['data'])
        else:
            contract.validate_execution(result['code'], result['data'])


@pytest.mark.parametrize('path', sorted(FIXTURES.glob('*.json')), ids=lambda p: p.stem)
def test_v2_shared_contract(path):
    payload = json.loads(path.read_text())
    if path.stem == 'protocol_constants':
        assert payload['protocolVersion'] == 2
        assert payload['actionPageSize'] == contract.ACTION_PAGE_SIZE
        assert 'actions.execute' in payload['actionIds'] and 'scripts.execute' not in payload['actionIds']
        return
    if path.stem.startswith('invalid_'):
        with pytest.raises(ValueError):
            validate(payload)
        return
    if path.stem == 'hello_v1_rejected':
        # The envelope is valid; rejection belongs to version negotiation, not JSON parsing.
        assert parse_message(payload)
        with pytest.raises(AssertionError):
            validate(payload)
        return
    validate(payload)
    message = parse_message(payload)
    wire = encode_message(message)
    assert len(wire) - 1 <= 4096
    assert decode_message(wire[:-1]) == message


@pytest.mark.parametrize('value', [None, True, ['clipboard', 'clipboard'], ['unknown'], [['script']]])
def test_supported_types_reject_malformed(value):
    with pytest.raises(ValueError):
        contract.validate_supported_types(value)


@pytest.mark.parametrize('field,value', [
    ('clipboardWritten', 1), ('pasteSent', 1), ('pasteSent', None), ('exitCode', 0),
    ('type', 'script'), ('actionId', 'script.test.0'), ('reason', 'WRITE_FAILED'),
])
def test_clipboard_cannot_claim_false_success(field, value):
    data = load('clipboard_execute_success')['result']['data']
    data[field] = value
    with pytest.raises(ValueError):
        contract.validate_execution('OK', data)


@pytest.mark.parametrize('field', ['type', 'exitCode', 'clipboardWritten', 'pasteSent', 'reason'])
def test_required_nullable_execution_fields(field):
    data = load('clipboard_execute_success')['result']['data']
    del data[field]
    with pytest.raises(ValueError):
        contract.validate_execution('OK', data)


@pytest.mark.parametrize('change', [dict(type='script'), dict(actionId='clipboard.'), dict(name='bad\0name'),
                                  dict(key='N'), dict(effectiveKey='x'), dict(content='secret')])
def test_page_item_boundaries(change):
    data = load('clipboard_page_first')['result']['data']
    data['actions'][0].update(change)
    with pytest.raises(ValueError):
        contract.validate_page(data)


def test_escaped_maximum_page_and_duplicate_id():
    source = load('clipboard_page_first')
    source['execId'] = '"' * 128
    for i, item in enumerate(source['result']['data']['actions']):
        item['name'] = '\\"' * 32
        item['actionId'] = 'clipboard.' + str(i) + 'x' * 53
    validate(source)
    assert len(encode_message(parse_message(source))) - 1 <= 4096
    source['result']['data']['actions'][1] = copy.deepcopy(source['result']['data']['actions'][0])
    with pytest.raises(ValueError):
        validate(source)


@pytest.mark.parametrize('action,payload', [
    ('actions.list', {'type': 'clipboard', 'offset': 0, 'content': 'secret'}),
    ('actions.list', {'type': 'clipboard', 'offset': 2**64}),
    ('actions.execute', {'actionId': 'system.time.read'}),
    ('actions.shortcut.execute', {'key': None}),
    ('actions.shortcut.execute', {'key': 'N'}),
])
def test_request_rejects_overrides_and_invalid_targets(action, payload):
    with pytest.raises(ValueError):
        contract.validate_request(action, payload)
