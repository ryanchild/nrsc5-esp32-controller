from pathlib import Path
from unittest.mock import patch

from fastapi.testclient import TestClient

from app.main import create_app
from app.parser import parse_nrsc5_line
from app.state import StateStore
from app.tuner import TunerManager


def make_client(tmp_path: Path) -> tuple[TestClient, StateStore, TunerManager]:
    store = StateStore(
        state_file=str(tmp_path / "state.json"),
        default_freq=90.3,
        default_program=0,
    )
    tuner = TunerManager(store=store, debounce_sec=0.0, enable_processes=False)
    app = create_app(store=store, tuner=tuner)
    return TestClient(app), store, tuner


def test_get_status_contract(tmp_path: Path) -> None:
    client, store, _ = make_client(tmp_path)
    parse_nrsc5_line("Station name: KEXP-FM", store)
    parse_nrsc5_line("Callsign: KEXP", store)
    parse_nrsc5_line("Title: Song", store)
    parse_nrsc5_line("Artist: Artist", store)
    parse_nrsc5_line("Album: Album", store)
    parse_nrsc5_line("Audio program 0: public, type: Music", store)
    parse_nrsc5_line("MER: 12.3 dB", store)
    parse_nrsc5_line("BER: 0.0001", store)

    res = client.get("/api/status", headers={"Host": "192.168.1.50:8751"})
    assert res.status_code == 200
    data = res.json()

    expected_keys = {
        "state",
        "freq",
        "program",
        "station_name",
        "callsign",
        "slogan",
        "title",
        "artist",
        "album",
        "genre",
        "programs",
        "mer_db",
        "ber",
        "synced",
        "stream_url",
        "updated_at",
    }
    assert set(data.keys()) == expected_keys
    assert data["state"] == "playing"
    assert data["freq"] == 90.3
    assert data["program"] == 0
    assert data["station_name"] == "KEXP-FM"
    assert data["callsign"] == "KEXP"
    assert data["title"] == "Song"
    assert data["artist"] == "Artist"
    assert data["album"] == "Album"
    assert data["programs"] == [{"index": 0, "name": "", "type": "Music"}]
    assert data["mer_db"] == 12.3
    assert data["ber"] == 0.0001
    assert data["synced"] is True
    assert data["stream_url"] == "http://192.168.1.50:8750/hdradio"
    assert isinstance(data["updated_at"], int)


def test_tune_post_and_get_and_persistence(tmp_path: Path) -> None:
    client, store, _ = make_client(tmp_path)
    parse_nrsc5_line("Title: Old Song", store)

    res = client.post("/api/tune", json={"freq": 94.9, "program": 1})
    assert res.status_code == 200
    body = res.json()
    assert body["state"] == "tuning"
    assert body["freq"] == 94.9
    assert body["program"] == 1
    assert body["title"] == ""

    res2 = client.get("/api/tune?freq=88.5&program=2")
    assert res2.status_code == 200
    body2 = res2.json()
    assert body2["state"] == "tuning"
    assert body2["freq"] == 88.5
    assert body2["program"] == 2

    reloaded = StateStore(state_file=str(tmp_path / "state.json"))
    snap = reloaded.snapshot()
    assert snap["freq"] == 88.5
    assert snap["program"] == 2


def test_tune_validation_errors(tmp_path: Path) -> None:
    client, _, _ = make_client(tmp_path)

    assert client.post("/api/tune", json={"freq": 87.1, "program": 0}).status_code == 400
    assert client.post("/api/tune", json={"freq": 108.5, "program": 0}).status_code == 400
    assert client.get("/api/tune?freq=109.1&program=0").status_code == 400

    assert client.post("/api/tune", json={"freq": 90.3, "program": -1}).status_code == 400
    assert client.post("/api/tune", json={"freq": 90.3, "program": 8}).status_code == 400
    assert client.get("/api/tune?freq=90.3&program=9").status_code == 400

    assert client.get("/api/tune").status_code == 400


def test_health_and_dashboard(tmp_path: Path) -> None:
    client, _, _ = make_client(tmp_path)

    res = client.get("/api/health")
    assert res.status_code == 200
    data = res.json()
    assert data["ok"] is True
    assert isinstance(data["nrsc5_running"], bool)
    assert isinstance(data["ffmpeg_running"], bool)
    assert isinstance(data["device_present"], bool)

    dash = client.get("/")
    assert dash.status_code == 200
    assert "NRSC-5 HD Radio" in dash.text


def test_debounce_rapid_tunes(tmp_path: Path) -> None:
    store = StateStore(state_file=str(tmp_path / "state.json"))
    tuner = TunerManager(store=store, debounce_sec=0.15, enable_processes=True)

    with patch.object(tuner, "_spawn_nrsc5") as mock_spawn:
        tuner._running = True
        tuner.request_tune(90.3, 0)
        tuner.request_tune(90.5, 0)
        tuner.request_tune(90.7, 1)

        import time
        time.sleep(0.3)

        assert mock_spawn.call_count == 1
        mock_spawn.assert_called_once_with(90.7, 1)
        tuner.stop()
