import base64
import datetime
import json
import logging
import threading
import time
import urllib.request
from typing import Any, Dict, Optional

logger = logging.getLogger(__name__)


def build_cpr_performer(soloists: str, orchestra: str, conductor: str) -> str:
    """Combine soloists, orchestra, and conductor into a readable performer string."""
    parts = []
    if soloists:
        parts.append(soloists)
    if orchestra:
        if conductor:
            parts.append(f"{orchestra}, {conductor}, cond.")
        else:
            parts.append(orchestra)
    elif conductor:
        parts.append(f"{conductor}, cond.")
    return "  ·  ".join(parts)


class MetadataEnricher:
    """Fetches enriched program and classical metadata in the background for supported stations."""

    def __init__(self, debounce_sec: float = 2.0) -> None:
        self.debounce_sec = debounce_sec
        self._lock = threading.Lock()
        self._last_query_time = 0.0
        self._last_title = ""
        self._cached_classical: Dict[str, Any] = {}

    def maybe_enrich(self, store: Any) -> None:
        """Trigger async enrichment if current tuned station has an enricher."""
        freq, prog = store.get_freq_program()
        # KVOD CPR Classical is on 90.1 HD2 (program index 1)
        if abs(freq - 90.1) < 0.05 and prog == 1:
            threading.Thread(
                target=self._enrich_cpr_classical,
                args=(store,),
                daemon=True,
                name="cpr-classical-enricher",
            ).start()
        # Indie 102.3 is on 102.3 HD1 (program index 0)
        elif abs(freq - 102.3) < 0.05 and prog == 0:
            threading.Thread(
                target=self._enrich_cpr_indie,
                args=(store,),
                daemon=True,
                name="cpr-indie-enricher",
            ).start()

    def _enrich_cpr_classical(self, store: Any) -> None:
        with self._lock:
            now = time.monotonic()
            if now - self._last_query_time < self.debounce_sec:
                return
            self._last_query_time = now

        try:
            from zoneinfo import ZoneInfo

            today_str = datetime.datetime.now(ZoneInfo("America/Denver")).strftime("%Y-%m-%d")
        except Exception:
            today_str = datetime.date.today().strftime("%Y-%m-%d")

        b64_date = base64.b64encode(today_str.encode("utf-8")).decode("utf-8")
        url = f"https://playlist.cprnetwork.org/api/playlistCL/selectData:{b64_date}"

        try:
            req = urllib.request.Request(
                url,
                headers={"User-Agent": "Mozilla/5.0 (compatible; nrsc5-tuner/1.0)"},
            )
            with urllib.request.urlopen(req, timeout=4.0) as resp:
                data = json.loads(resp.read().decode("utf-8"))
            if not isinstance(data, list) or not data:
                return

            latest = data[0]
            composer = latest.get("composer", "").strip()
            title = latest.get("title", "").strip()
            album = latest.get("album", "").strip()
            conductor = latest.get("conductor", "").strip()
            orchestra = latest.get("orchestra", "").strip()
            soloists = latest.get("soloists", "").strip()
            show = latest.get("line_2", "").strip()
            if show in ("CPR.org", "Music"):
                show = ""

            performer = build_cpr_performer(soloists, orchestra, conductor)

            # Explicitly set or clear all track-specific fields so previous piece data doesn't persist
            updates: Dict[str, Any] = {
                "composer": composer,
                "title": title or store._state.title,
                "album": album,
                "conductor": conductor,
                "orchestra": orchestra,
                "soloists": soloists,
                "performer": performer,
                "show": show,
            }
            if composer:
                updates["artist"] = composer
            elif performer:
                updates["artist"] = performer

            logger.info(
                "[enricher] Enriched KVOD: %s - %s (%s)",
                composer,
                title,
                performer or "no performer",
            )
            store.update(**updates)
        except Exception as exc:
            logger.debug("CPR Classical enrichment failed: %s", exc)

    def _enrich_cpr_indie(self, store: Any) -> None:
        with self._lock:
            now = time.monotonic()
            if now - self._last_query_time < self.debounce_sec:
                return
            self._last_query_time = now

        try:
            from zoneinfo import ZoneInfo

            today_str = datetime.datetime.now(ZoneInfo("America/Denver")).strftime("%Y-%m-%d")
        except Exception:
            today_str = datetime.date.today().strftime("%Y-%m-%d")

        b64_date = base64.b64encode(today_str.encode("utf-8")).decode("utf-8")
        url = f"https://playlist.cprnetwork.org/api/playlistCO/selectData:{b64_date}"

        try:
            req = urllib.request.Request(
                url,
                headers={"User-Agent": "Mozilla/5.0 (compatible; nrsc5-tuner/1.0)"},
            )
            with urllib.request.urlopen(req, timeout=4.0) as resp:
                data = json.loads(resp.read().decode("utf-8"))
            if not isinstance(data, list) or not data:
                return

            latest = data[0]
            show = latest.get("line_2", "").strip()
            if show in ("Indie 102.3", "Music"):
                show = ""
            album = latest.get("album", "").strip()
            updates: Dict[str, Any] = {}
            if show:
                updates["show"] = show
            if album and not store._state.album:
                updates["album"] = album
            if updates:
                store.update(**updates)
        except Exception as exc:
            logger.debug("CPR Indie enrichment failed: %s", exc)
