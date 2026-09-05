from adv_helper.core.session import ConnectionSession


def test_disconnect_clears_pending_and_business_data():
    session = ConnectionSession()
    session.connect()
    assert session.begin_request("exec-1")
    session.disconnect()
    assert not session.connected
    assert session.pending_exec_ids == set()


def test_late_or_duplicate_completion_is_ignored():
    session = ConnectionSession()
    session.connect()
    assert session.begin_request("exec-1")
    assert not session.begin_request("exec-1")
    assert session.complete_request("exec-1")
    assert not session.complete_request("exec-1")
