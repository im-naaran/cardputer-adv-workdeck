from adv_helper.os_adapters import computer_identity


def test_platform_uuid_produces_stable_id_independent_of_display_name(monkeypatch):
    monkeypatch.setattr(computer_identity, "_macos_platform_uuid", lambda: "PLATFORM-UUID")
    monkeypatch.setattr(computer_identity.platform, "node", lambda: "First Name")
    first = computer_identity.load_computer_identity()
    monkeypatch.setattr(computer_identity.platform, "node", lambda: "Renamed Mac")
    second = computer_identity.load_computer_identity()
    assert first.stable_id == second.stable_id
    assert first.display_name != second.display_name
    assert "PLATFORM-UUID" not in first.stable_id
