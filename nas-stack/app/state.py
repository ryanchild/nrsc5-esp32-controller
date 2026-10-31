import json
import logging
import os
import threading
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Dict, List, Optional

logger = logging.getLogger(__name__)


@dataclass
class ProgramInfo:
    index: int
    name: str = ""
    type: str = ""

    def to_dict(self) -> Dict[str, Any]:
        return {
            "index": self.index,
            "name": self.name,
            "type": self.type,
        }


@dataclass
class TunerState:
    state: str = "tuning"  # "tuning" | "playing" | "no_signal" | "error"
    freq: float = 90.3
    program: int = 0
    station_name: str = ""
    callsign: str = ""
    slogan: str = ""
    title: str = ""
    artist: str = ""
    album: str = ""
    genre: str = ""
    composer: str = ""
    conductor: str = ""
    orchestra: str = ""
    soloists: str = ""
    performer: str = ""
    show: str = ""
    programs: Dict[int, ProgramInfo] = field(default_factory=dict)
    mer_db: Optional[float] = None
    ber: Optional[float] = None
    synced: bool = False
    updated_at: int = field(default_factory=lambda: int(time.time()))


class StateStore:
    """Thread-safe holder for current tuner state and persistent station settings."""

    def __init__(
        self,
        state_file: Optional[str] = None,
        default_freq: Optional[float] = None,
        default_program: Optional[int] = None,
    ) -> None:
        self._lock = threading.RLock()
        self.state_file = state_file or os.environ.get("STATE_FILE", "/data/state.json")

        env_freq = float(os.environ.get("DEFAULT_FREQ", "90.3"))
        env_prog = int(os.environ.get("DEFAULT_PROGRAM", "0"))
        init_freq = default_freq if default_freq is not None else env_freq
        init_prog = default_program if default_program is not None else env_prog

        self._state = TunerState(
            state="tuning",
            freq=round(init_freq, 1),
            program=init_prog,
            programs={init_prog: ProgramInfo(index=init_prog, name="", type="")},
        )
        self.load_persisted()

    def load_persisted(self) -> None:
        """Load persisted freq and program from state_file if present."""
        if not self.state_file:
            return
        path = Path(self.state_file)
        if not path.exists():
            return
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            with self._lock:
                if "freq" in data:
                    freq = float(data["freq"])
                    if 87.5 <= freq <= 108.0:
                        self._state.freq = round(freq, 1)
                if "program" in data:
                    prog = int(data["program"])
                    if 0 <= prog <= 7:
                        self._state.program = prog
                if self._state.program not in self._state.programs:
                    self._state.programs[self._state.program] = ProgramInfo(
                        index=self._state.program
                    )
        except Exception as exc:
            logger.warning("Failed to load persisted state from %s: %s", self.state_file, exc)

    def save_persisted(self) -> None:
        """Persist current freq and program to state_file."""
        if not self.state_file:
            return
        path = Path(self.state_file)
        try:
            path.parent.mkdir(parents=True, exist_ok=True)
            with self._lock:
                payload = {
                    "freq": self._state.freq,
                    "program": self._state.program,
                    "updated_at": self._state.updated_at,
                }
            tmp_path = path.with_suffix(".tmp")
            tmp_path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
            tmp_path.replace(path)
        except Exception as exc:
            logger.warning("Failed to persist state to %s: %s", self.state_file, exc)

    def reset_for_tune(self, freq: float, program: int) -> None:
        """Reset transient station/PSD metadata when tuning to a new frequency or program."""
        with self._lock:
            freq_changed = abs(self._state.freq - freq) > 1e-4
            self._state.freq = round(freq, 1)
            self._state.program = int(program)
            self._state.state = "tuning"
            self._state.title = ""
            self._state.artist = ""
            self._state.album = ""
            self._state.genre = ""
            self._state.composer = ""
            self._state.conductor = ""
            self._state.orchestra = ""
            self._state.soloists = ""
            self._state.performer = ""
            self._state.show = ""
            if freq_changed:
                self._state.station_name = ""
                self._state.callsign = ""
                self._state.slogan = ""
                self._state.mer_db = None
                self._state.ber = None
                self._state.synced = False
                self._state.programs = {
                    self._state.program: ProgramInfo(index=self._state.program)
                }
            elif self._state.program not in self._state.programs:
                self._state.programs[self._state.program] = ProgramInfo(
                    index=self._state.program
                )
            self._state.updated_at = int(time.time())
        self.save_persisted()

    def update(self, **kwargs: Any) -> None:
        """Update specific fields on TunerState thread-safely."""
        with self._lock:
            changed = False
            for key, value in kwargs.items():
                if hasattr(self._state, key):
                    if getattr(self._state, key) != value:
                        setattr(self._state, key, value)
                        changed = True
            if changed:
                self._state.updated_at = int(time.time())

    def upsert_program(
        self, index: int, name: Optional[str] = None, prog_type: Optional[str] = None
    ) -> None:
        """Add or update a discovered audio program entry."""
        if not (0 <= index <= 7):
            return
        with self._lock:
            prog = self._state.programs.get(index)
            if prog is None:
                prog = ProgramInfo(index=index)
                self._state.programs[index] = prog
            if name is not None and name != "":
                prog.name = name
            if prog_type is not None and prog_type != "":
                prog.type = prog_type
            self._state.updated_at = int(time.time())

    def get_freq_program(self) -> tuple[float, int]:
        with self._lock:
            return self._state.freq, self._state.program

    def snapshot(self, stream_url: str = "") -> Dict[str, Any]:
        """Return a dictionary matching the GET /api/status contract."""
        with self._lock:
            programs_list: List[Dict[str, Any]] = [
                p.to_dict()
                for _, p in sorted(self._state.programs.items(), key=lambda item: item[0])
            ]
            if not programs_list:
                programs_list = [
                    ProgramInfo(index=self._state.program, name="", type="").to_dict()
                ]
            return {
                "state": self._state.state,
                "freq": self._state.freq,
                "program": self._state.program,
                "station_name": self._state.station_name,
                "callsign": self._state.callsign,
                "slogan": self._state.slogan,
                "title": self._state.title,
                "artist": self._state.artist,
                "album": self._state.album,
                "genre": self._state.genre,
                "composer": self._state.composer,
                "conductor": self._state.conductor,
                "orchestra": self._state.orchestra,
                "soloists": self._state.soloists,
                "performer": self._state.performer,
                "show": self._state.show,
                "programs": programs_list,
                "mer_db": self._state.mer_db,
                "ber": self._state.ber,
                "synced": self._state.synced,
                "stream_url": stream_url,
                "updated_at": self._state.updated_at,
            }
