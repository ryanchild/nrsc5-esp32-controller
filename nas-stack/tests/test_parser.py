from pathlib import Path

from app.parser import parse_nrsc5_line
from app.state import StateStore


def make_store(tmp_path: Path) -> StateStore:
    return StateStore(
        state_file=str(tmp_path / "state.json"),
        default_freq=90.3,
        default_program=0,
    )


def test_parse_station_metadata(tmp_path: Path) -> None:
    store = make_store(tmp_path)

    res1 = parse_nrsc5_line("14:22:01 Station name: KEXP-FM", store)
    assert res1 == {"station_name": "KEXP-FM"}
    snap = store.snapshot()
    assert snap["station_name"] == "KEXP-FM"
    assert snap["callsign"] == "KEXP"

    res2 = parse_nrsc5_line("14:22:02 Callsign: KEXP", store)
    assert res2 == {"callsign": "KEXP"}

    res3 = parse_nrsc5_line("14:22:03 Slogan: Where the Music Matters", store)
    assert res3 == {"slogan": "Where the Music Matters"}
    assert store.snapshot()["slogan"] == "Where the Music Matters"


def test_parse_psd_metadata(tmp_path: Path) -> None:
    store = make_store(tmp_path)

    parse_nrsc5_line("Title: Idioteque", store)
    parse_nrsc5_line("Artist: Radiohead", store)
    parse_nrsc5_line("Album: Kid A", store)
    parse_nrsc5_line("Genre: Alternative", store)

    snap = store.snapshot()
    assert snap["title"] == "Idioteque"
    assert snap["artist"] == "Radiohead"
    assert snap["album"] == "Kid A"
    assert snap["genre"] == "Alternative"
    assert snap["state"] == "playing"
    assert snap["synced"] is True


def test_parse_mer_and_ber_variants(tmp_path: Path) -> None:
    store = make_store(tmp_path)

    res = parse_nrsc5_line("12:00:01 MER: 12.3 dB", store)
    assert res == {"mer_db": 12.3}
    assert store.snapshot()["mer_db"] == 12.3

    res2 = parse_nrsc5_line("12:00:02 MER: 11.0 dB (lower), 13.6 dB (upper)", store)
    assert res2 == {"mer_db": 12.3}
    assert store.snapshot()["mer_db"] == 12.3

    res3 = parse_nrsc5_line("MER: 10.0 dB, 14.0 dB", store)
    assert res3 == {"mer_db": 12.0}
    assert store.snapshot()["mer_db"] == 12.0

    res4 = parse_nrsc5_line("12:00:03 BER: 0.000125, avg: 0.000210, min: 0.000050, max: 0.000900", store)
    assert res4 == {"ber": 0.000125}
    assert store.snapshot()["ber"] == 0.000125


def test_parse_sync_and_programs(tmp_path: Path) -> None:
    store = make_store(tmp_path)
    assert store.snapshot()["state"] == "tuning"
    assert store.snapshot()["synced"] is False

    parse_nrsc5_line("12:00:00 Synchronized", store)
    snap = store.snapshot()
    assert snap["synced"] is True
    assert snap["state"] == "playing"

    parse_nrsc5_line("12:00:01 Audio program 0: public, type: Music, sound experience 0", store)
    parse_nrsc5_line("12:00:01 Audio program 1: name: KEXP-HD2, type: Rock", store)
    snap = store.snapshot()
    assert snap["programs"] == [
        {"index": 0, "name": "", "type": "Music"},
        {"index": 1, "name": "KEXP-HD2", "type": "Rock"},
    ]

    parse_nrsc5_line("12:00:10 Lost synchronization", store)
    snap = store.snapshot()
    assert snap["synced"] is False
    assert snap["state"] == "no_signal"


def test_retune_clears_psd(tmp_path: Path) -> None:
    store = make_store(tmp_path)
    parse_nrsc5_line("Station name: KEXP-FM", store)
    parse_nrsc5_line("Title: Track 1", store)
    parse_nrsc5_line("Artist: Artist 1", store)
    parse_nrsc5_line("Album: Album 1", store)

    store.reset_for_tune(90.3, 1)
    snap = store.snapshot()
    assert snap["state"] == "tuning"
    assert snap["freq"] == 90.3
    assert snap["program"] == 1
    assert snap["title"] == ""
    assert snap["artist"] == ""
    assert snap["album"] == ""
    assert snap["composer"] == ""
    assert snap["performer"] == ""
    assert snap["show"] == ""
    assert snap["station_name"] == "KEXP-FM"

    store.reset_for_tune(94.9, 0)
    snap2 = store.snapshot()
    assert snap2["freq"] == 94.9
    assert snap2["program"] == 0
    assert snap2["station_name"] == ""
    assert snap2["synced"] is False


def test_cpr_enricher_formatting() -> None:
    from app.enricher import build_cpr_performer
    assert (
        build_cpr_performer("Itzhak Perlman (violin)", "Boston Symphony Orchestra", "Seiji Ozawa")
        == "Itzhak Perlman (violin)  ·  Boston Symphony Orchestra, Seiji Ozawa, cond."
    )
    assert build_cpr_performer("", "Colorado Symphony", "Brett Mitchell") == "Colorado Symphony, Brett Mitchell, cond."
    assert build_cpr_performer("Yo-Yo Ma (cello)", "", "") == "Yo-Yo Ma (cello)"


def test_station_promo_title_filtering(tmp_path: Path) -> None:
    store = make_store(tmp_path)
    # Set an initial real track title
    parse_nrsc5_line("Title: Symphony No. 5 in C Minor, Op. 67", store)
    parse_nrsc5_line("Artist: Ludwig van Beethoven", store)
    assert store.snapshot()["title"] == "Symphony No. 5 in C Minor, Op. 67"
    assert store.snapshot()["artist"] == "Ludwig van Beethoven"

    # nrsc5 sends periodic station promo / website lines
    res1 = parse_nrsc5_line("Title: CPR Classical / cprclassical.org", store)
    assert "title" not in res1
    assert "station_promo" in res1
    # Real piece title must NOT have been overwritten
    assert store.snapshot()["title"] == "Symphony No. 5 in C Minor, Op. 67"

    # More promo variations
    parse_nrsc5_line("Title: cprclassical.org", store)
    assert store.snapshot()["title"] == "Symphony No. 5 in C Minor, Op. 67"

    parse_nrsc5_line("Title: CPR Classical", store)
    assert store.snapshot()["title"] == "Symphony No. 5 in C Minor, Op. 67"

    parse_nrsc5_line("Artist: CPR Classical / cprclassical.org", store)
    assert store.snapshot()["artist"] == "Ludwig van Beethoven"

    # When a new real piece arrives, title updates normally
    parse_nrsc5_line("Title: Violin Concerto in D Major, Op. 35", store)
    assert store.snapshot()["title"] == "Violin Concerto in D Major, Op. 35"

    # When enhanced classical metadata is present (composer is set),
    # over-the-air PSD cannot overwrite the enhanced database title
    store.update(composer="Pyotr Ilyich Tchaikovsky", title="Swan Lake, Op. 20: Act II")
    parse_nrsc5_line("Title: Swan Lake (Excerpts)", store)
    assert store.snapshot()["title"] == "Swan Lake, Op. 20: Act II"


def test_cpr_enricher_clears_stale_orchestra_and_conductor(tmp_path: Path) -> None:
    from unittest.mock import MagicMock, patch
    from app.enricher import MetadataEnricher
    import json

    store = make_store(tmp_path)
    store.update(freq=90.1, program=1)
    enricher = MetadataEnricher()

    data_orchestral = [
        {
            "composer": "Ludwig van Beethoven",
            "title": "Minuet in G WoO 10/2",
            "album": "Boccherini / Tchaikovsky Chamber Orchestra",
            "orchestra": "Philadelphia Orchestra",
            "conductor": "Eugene Ormandy",
            "soloists": "",
            "line_2": "",
        }
    ]

    mock_resp1 = MagicMock()
    mock_resp1.read.return_value = json.dumps(data_orchestral).encode("utf-8")
    mock_resp1.__enter__.return_value = mock_resp1

    with patch("urllib.request.urlopen", return_value=mock_resp1):
        enricher._enrich_cpr_classical(store)

    snap1 = store.snapshot()
    assert snap1["composer"] == "Ludwig van Beethoven"
    assert snap1["title"] == "Minuet in G WoO 10/2"
    assert snap1["orchestra"] == "Philadelphia Orchestra"
    assert snap1["conductor"] == "Eugene Ormandy"

    # Now simulate the next track: Yo-Yo Ma solo cello
    data_solo = [
        {
            "composer": "Johann Sebastian Bach",
            "title": "Cello Suite #1 in G BWV 1007",
            "album": "Six Evolutions - Bach: Cello Suites",
            "orchestra": "",
            "conductor": "",
            "soloists": "Yo-Yo Ma, cello",
            "line_2": "",
        }
    ]

    mock_resp2 = MagicMock()
    mock_resp2.read.return_value = json.dumps(data_solo).encode("utf-8")
    mock_resp2.__enter__.return_value = mock_resp2

    # Allow debounced query
    enricher._last_query_time = 0.0
    with patch("urllib.request.urlopen", return_value=mock_resp2):
        enricher._enrich_cpr_classical(store)

    snap2 = store.snapshot()
    assert snap2["composer"] == "Johann Sebastian Bach"
    assert snap2["title"] == "Cello Suite #1 in G BWV 1007"
    assert snap2["album"] == "Six Evolutions - Bach: Cello Suites"
    assert snap2["soloists"] == "Yo-Yo Ma, cello"
    # Stale orchestra and conductor from Beethoven MUST be cleared!
    assert snap2["orchestra"] == ""
    assert snap2["conductor"] == ""
    assert snap2["performer"] == "Yo-Yo Ma, cello"


def test_direct_audio_streaming_preserves_frames_and_alignment(tmp_path: Path) -> None:
    from unittest.mock import MagicMock
    from app.tuner import TunerManager

    store = StateStore(state_file=str(tmp_path / "state.json"))
    store.update(state="tuning", synced=False)
    tuner = TunerManager(store=store, debounce_sec=0.0, enable_processes=False)
    tuner._running = True

    # Construct mock proc
    mock_proc = MagicMock()
    mock_proc.poll.return_value = None

    # Chunk 1: 44-byte RIFF header + 5 bytes of PCM
    chunk1 = b"RIFF\x24\x00\x00\x00WAVE" + (b"\x00" * 32) + b"\x01\x02\x03\x04\x05"
    # Chunk 2: 3 bytes of PCM -> completes the partial frame (5 + 3 = 8 bytes = two 4-byte frames)
    chunk2 = b"\x06\x07\x08"
    mock_proc.stdout.read.side_effect = [chunk1, chunk2, b""]

    written_chunks = []
    tuner._write_pcm_to_ffmpeg = lambda data: written_chunks.append(data)

    tuner._read_nrsc5_stdout(mock_proc, gen=0)

    # First call had 5 PCM bytes -> stripped header -> sent 4 bytes, kept 1 byte
    # Second call had 3 PCM bytes -> combined with 1 leftover = 4 bytes -> sent 4 bytes
    assert len(written_chunks) == 2
    assert written_chunks[0] == b"\x01\x02\x03\x04"
    assert written_chunks[1] == b"\x05\x06\x07\x08"
    assert sum(len(c) for c in written_chunks) == 8
    assert all(len(c) % 4 == 0 for c in written_chunks)

    # State updated to playing and synced
    assert store._state.state == "playing"
    assert store._state.synced is True
    assert tuner._last_audio_time > 0.0





