import re
from typing import Any, Dict, Optional

from app.state import StateStore

# Matches optional nrsc5 timestamp prefix like "12:34:56 " or "2026-10-29T12:34:56Z " or "[INFO] "
_PREFIX_RE = re.compile(
    r"^(?:\d{4}-\d{2}-\d{2}[T\s]\d{2}:\d{2}:\d{2}(?:\.\d+)?Z?\s+|\d{2}:\d{2}:\d{2}(?:\.\d+)?\s+)?(?:\[[A-Za-z0-9_+-]+\]\s+)?"
)

_STATION_NAME_RE = re.compile(r"^Station name:\s*(.+?)\s*$", re.IGNORECASE)
_CALLSIGN_RE = re.compile(r"^(?:Callsign|Station callsign):\s*(.+?)\s*$", re.IGNORECASE)
_SLOGAN_RE = re.compile(r"^Slogan:\s*(.+?)\s*$", re.IGNORECASE)

_TITLE_RE = re.compile(r"^Title:\s*(.*?)\s*$", re.IGNORECASE)
_ARTIST_RE = re.compile(r"^Artist:\s*(.*?)\s*$", re.IGNORECASE)
_ALBUM_RE = re.compile(r"^Album:\s*(.*?)\s*$", re.IGNORECASE)
_GENRE_RE = re.compile(r"^Genre:\s*(.*?)\s*$", re.IGNORECASE)

_MER_RE = re.compile(
    r"^MER:\s*(-?\d+(?:\.\d+)?)\s*dB(?:\s*(?:\(lower\))?\s*,\s*(-?\d+(?:\.\d+)?)\s*dB(?:\s*\(upper\))?)?",
    re.IGNORECASE,
)

_BER_RE = re.compile(r"^BER:\s*([0-9.eE+-]+)", re.IGNORECASE)

_SYNC_GAINED_RE = re.compile(r"^Synchronized\b", re.IGNORECASE)
_SYNC_LOST_RE = re.compile(r"^Lost synchronization\b", re.IGNORECASE)

_AUDIO_PROG_RE = re.compile(
    r"^Audio (?:program|service)\s+(\d+)\s*:\s*(.+?)\s*$", re.IGNORECASE
)
_TYPE_FIELD_RE = re.compile(r"\btype:\s*([^,]+)", re.IGNORECASE)
_NAME_FIELD_RE = re.compile(r"\bname:\s*([^,]+)", re.IGNORECASE)

_PROMO_URL_RE = re.compile(
    r"(?:https?://|www\.|\b[a-z0-9_.-]+\.(?:org|com|net|fm|edu|gov|io)\b)",
    re.IGNORECASE,
)

_KNOWN_PROMO_PHRASES = {
    "cpr classical",
    "cpr news",
    "colorado public radio",
    "indie 102.3",
    "krfc 88.9",
    "krfc 88.9 fm",
}


def is_station_promo(text: str, store: Optional[StateStore] = None) -> bool:
    """Detect if a title/artist string is a station promo, website, or callsign rather than a real track."""
    t = text.strip().lower()
    if not t:
        return True
    if _PROMO_URL_RE.search(t):
        return True
    if t in _KNOWN_PROMO_PHRASES:
        return True
    for p in _KNOWN_PROMO_PHRASES:
        if t.startswith(f"{p} /") or t.startswith(f"{p} -") or t.startswith(f"{p} ·"):
            return True
    if store is not None:
        callsign = store._state.callsign.lower().strip()
        st_name = store._state.station_name.lower().strip()
        slogan = store._state.slogan.lower().strip()
        if callsign and (t == callsign or t == f"{callsign}-fm" or t == f"{callsign} fm"):
            return True
        if st_name and t == st_name:
            return True
        if slogan and t == slogan:
            return True
    return False


def strip_log_prefix(line: str) -> str:
    """Strip timestamp and log level prefixes from an nrsc5 stderr line."""
    cleaned = line.strip()
    return _PREFIX_RE.sub("", cleaned).strip()


def parse_nrsc5_line(line: str, store: Optional[StateStore] = None) -> Dict[str, Any]:
    """Parse a single line of nrsc5 stderr output.

    Returns a dictionary of parsed events/fields and applies updates to `store` if provided.
    """
    text = strip_log_prefix(line)
    if not text:
        return {}

    parsed: Dict[str, Any] = {}

    m = _STATION_NAME_RE.match(text)
    if m:
        val = m.group(1).strip()
        parsed["station_name"] = val
        if store is not None:
            updates: Dict[str, Any] = {"station_name": val}
            if not store._state.callsign:
                updates["callsign"] = val.split("-")[0].strip()
            store.update(**updates)
        return parsed

    m = _CALLSIGN_RE.match(text)
    if m:
        val = m.group(1).strip()
        parsed["callsign"] = val
        if store is not None:
            store.update(callsign=val)
        return parsed

    m = _SLOGAN_RE.match(text)
    if m:
        val = m.group(1).strip()
        parsed["slogan"] = val
        if store is not None:
            store.update(slogan=val)
        return parsed

    m = _TITLE_RE.match(text)
    if m:
        val = m.group(1).strip()
        if is_station_promo(val, store):
            # Save to slogan if empty for display, but never overwrite a real song title
            if store is not None and not store._state.slogan:
                store.update(slogan=val)
            parsed["station_promo"] = val
            return parsed

        parsed["title"] = val
        if store is not None:
            store.update(title=val, state="playing", synced=True)
        return parsed

    m = _ARTIST_RE.match(text)
    if m:
        val = m.group(1).strip()
        if is_station_promo(val, store):
            parsed["station_promo"] = val
            return parsed

        parsed["artist"] = val
        if store is not None:
            store.update(artist=val)
        return parsed

    m = _ALBUM_RE.match(text)
    if m:
        val = m.group(1).strip()
        parsed["album"] = val
        if store is not None:
            store.update(album=val)
        return parsed

    m = _GENRE_RE.match(text)
    if m:
        val = m.group(1).strip()
        parsed["genre"] = val
        if store is not None:
            store.update(genre=val)
        return parsed

    m = _MER_RE.match(text)
    if m:
        lower = float(m.group(1))
        upper = float(m.group(2)) if m.group(2) is not None else None
        mer_db = round((lower + upper) / 2.0, 2) if upper is not None else round(lower, 2)
        parsed["mer_db"] = mer_db
        if store is not None:
            store.update(mer_db=mer_db)
        return parsed

    m = _BER_RE.match(text)
    if m:
        try:
            ber = float(m.group(1))
            parsed["ber"] = ber
            if store is not None:
                store.update(ber=ber)
        except ValueError:
            pass
        return parsed

    if _SYNC_GAINED_RE.match(text):
        parsed["synced"] = True
        if store is not None:
            current_state = store._state.state
            next_state = "playing" if current_state in ("tuning", "no_signal") else current_state
            store.update(synced=True, state=next_state)
        return parsed

    if _SYNC_LOST_RE.match(text):
        parsed["synced"] = False
        if store is not None:
            store.update(synced=False, state="no_signal")
        return parsed

    m = _AUDIO_PROG_RE.match(text)
    if m:
        idx = int(m.group(1))
        details = m.group(2).strip()
        prog_type = ""
        prog_name = ""

        tm = _TYPE_FIELD_RE.search(details)
        if tm:
            prog_type = tm.group(1).strip()

        nm = _NAME_FIELD_RE.search(details)
        if nm:
            prog_name = nm.group(1).strip()

        if not prog_type and not prog_name and "," not in details:
            prog_type = details

        parsed["audio_program"] = {
            "index": idx,
            "name": prog_name,
            "type": prog_type,
        }
        if store is not None:
            store.upsert_program(idx, name=prog_name, prog_type=prog_type)
        return parsed

    return parsed
