import logging
import os
from contextlib import asynccontextmanager
from typing import Any, Dict, Optional

from fastapi import FastAPI, HTTPException, Query, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import HTMLResponse
from pydantic import BaseModel

from app.state import StateStore
from app.tuner import TunerManager


class TuneRequest(BaseModel):
    freq: float
    program: int = 0


def validate_tune_params(freq: Optional[float], program: Optional[int]) -> tuple[float, int]:
    if freq is None:
        raise HTTPException(status_code=400, detail="Missing 'freq' parameter.")
    try:
        freq_val = float(freq)
        prog_val = int(program if program is not None else 0)
    except (TypeError, ValueError) as exc:
        raise HTTPException(status_code=400, detail="Invalid 'freq' or 'program' type.") from exc

    if not (87.5 <= freq_val <= 108.0):
        raise HTTPException(
            status_code=400,
            detail=f"Frequency {freq_val} out of range (must be 87.5 <= freq <= 108.0 MHz).",
        )
    if not (0 <= prog_val <= 7):
        raise HTTPException(
            status_code=400,
            detail=f"Program {prog_val} out of range (must be 0 <= program <= 7).",
        )
    return round(freq_val, 1), prog_val


def build_stream_url(request: Request) -> str:
    mount = os.environ.get("ICECAST_MOUNT", "/hdradio").strip() or "/hdradio"
    if not mount.startswith("/"):
        mount = "/" + mount
    icecast_port = os.environ.get("ICECAST_EXTERNAL_PORT", os.environ.get("ICECAST_PORT", "8750"))

    public_host = os.environ.get("PUBLIC_HOST", "").strip()
    if public_host:
        host = public_host
    else:
        host_header = request.headers.get("host", "")
        if host_header:
            host = host_header.split(":")[0]
        elif request.url.hostname:
            host = request.url.hostname
        else:
            host = "localhost"

    return f"http://{host}:{icecast_port}{mount}"


DASHBOARD_HTML = """<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>NRSC-5 HD Radio Controller</title>
  <style>
    :root {
      --bg: #111827;
      --card: #1f2937;
      --border: #374151;
      --text: #f9fafb;
      --muted: #9ca3af;
      --accent: #3b82f6;
      --accent-hover: #2563eb;
      --green: #10b981;
      --amber: #f59e0b;
      --red: #ef4444;
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
      background: var(--bg);
      color: var(--text);
      padding: 2rem 1rem;
    }
    .container { max-width: 640px; margin: 0 auto; }
    .card {
      background: var(--card);
      border: 1px solid var(--border);
      border-radius: 12px;
      padding: 1.5rem;
      margin-bottom: 1.25rem;
    }
    .header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 1rem; }
    h1 { font-size: 1.35rem; margin: 0; }
    .badge {
      padding: 0.25rem 0.65rem;
      border-radius: 999px;
      font-size: 0.8rem;
      font-weight: 600;
      text-transform: uppercase;
      background: var(--border);
    }
    .badge.playing { background: rgba(16, 185, 129, 0.2); color: var(--green); }
    .badge.tuning { background: rgba(245, 158, 11, 0.2); color: var(--amber); }
    .badge.no_signal, .badge.error { background: rgba(239, 68, 68, 0.2); color: var(--red); }
    .station-row { display: flex; align-items: baseline; gap: 0.75rem; margin-bottom: 0.5rem; }
    .freq-display { font-size: 2.2rem; font-weight: 700; }
    .prog-display { font-size: 1.2rem; color: var(--accent); font-weight: 600; }
    .callsign { font-size: 1.15rem; color: var(--muted); }
    .now-playing { margin-top: 1rem; padding-top: 1rem; border-top: 1px solid var(--border); }
    .title { font-size: 1.2rem; font-weight: 600; margin-bottom: 0.25rem; }
    .artist { color: var(--muted); font-size: 0.95rem; }
    .metrics {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 0.75rem;
      margin-top: 1rem;
      padding-top: 1rem;
      border-top: 1px solid var(--border);
      font-size: 0.85rem;
      color: var(--muted);
    }
    .metrics strong { color: var(--text); display: block; font-size: 1rem; margin-top: 0.15rem; }
    form { display: flex; flex-wrap: wrap; gap: 0.75rem; align-items: flex-end; }
    label { display: flex; flex-direction: column; font-size: 0.85rem; color: var(--muted); gap: 0.35rem; flex: 1; min-width: 120px; }
    input, select {
      padding: 0.6rem 0.75rem;
      border-radius: 8px;
      border: 1px solid var(--border);
      background: var(--bg);
      color: var(--text);
      font-size: 1rem;
    }
    button {
      padding: 0.65rem 1.25rem;
      border-radius: 8px;
      border: none;
      background: var(--accent);
      color: white;
      font-weight: 600;
      font-size: 0.95rem;
      cursor: pointer;
    }
    button:hover { background: var(--accent-hover); }
    a { color: var(--accent); text-decoration: none; }
    a:hover { text-decoration: underline; }
  </style>
</head>
<body>
  <div class="container">
    <div class="card">
      <div class="header">
        <h1>NRSC-5 HD Radio</h1>
        <span id="state-badge" class="badge">loading</span>
      </div>
      <div class="station-row">
        <span id="freq" class="freq-display">--.-</span>
        <span id="program" class="prog-display">HD1</span>
        <span id="station-name" class="callsign"></span>
      </div>
      <div id="slogan" class="artist"></div>
      <div class="now-playing">
        <div id="title" class="title">--</div>
        <div id="artist-album" class="artist">--</div>
      </div>
      <div class="metrics">
        <div>Sync<strong id="synced">--</strong></div>
        <div>MER<strong id="mer">--</strong></div>
        <div>BER<strong id="ber">--</strong></div>
      </div>
      <div style="margin-top: 1rem; font-size: 0.85rem;">
        Stream URL: <a id="stream-url" href="#" target="_blank">--</a>
      </div>
    </div>
    <div class="card">
      <h2 style="font-size: 1.05rem; margin-top: 0; margin-bottom: 1rem;">Tune Station</h2>
      <form id="tune-form">
        <label>
          Frequency (MHz)
          <input type="number" id="input-freq" step="0.2" min="87.5" max="108.0" value="90.3" required />
        </label>
        <label>
          HD Subchannel
          <select id="input-program">
            <option value="0">0 (HD1)</option>
            <option value="1">1 (HD2)</option>
            <option value="2">2 (HD3)</option>
            <option value="3">3 (HD4)</option>
          </select>
        </label>
        <button type="submit">Tune</button>
      </form>
    </div>
  </div>
  <script>
    let initializedForm = false;
    async function refreshStatus() {
      try {
        const res = await fetch('/api/status');
        const data = await res.json();
        const badge = document.getElementById('state-badge');
        badge.textContent = data.state;
        badge.className = 'badge ' + data.state;
        document.getElementById('freq').textContent = Number(data.freq).toFixed(1);
        document.getElementById('program').textContent = 'HD' + (Number(data.program) + 1);
        document.getElementById('station-name').textContent = data.station_name || data.callsign || '';
        document.getElementById('slogan').textContent = data.slogan || '';
        document.getElementById('title').textContent = data.title || 'No Title PSD';
        const parts = [data.artist, data.album].filter(Boolean);
        document.getElementById('artist-album').textContent = parts.length ? parts.join(' — ') : 'No Artist PSD';
        document.getElementById('synced').textContent = data.synced ? 'Locked' : 'Unlocked';
        document.getElementById('mer').textContent = data.mer_db !== null ? data.mer_db + ' dB' : '--';
        document.getElementById('ber').textContent = data.ber !== null ? data.ber : '--';
        const streamEl = document.getElementById('stream-url');
        streamEl.textContent = data.stream_url;
        streamEl.href = data.stream_url;
        if (!initializedForm) {
          document.getElementById('input-freq').value = data.freq;
          document.getElementById('input-program').value = data.program;
          initializedForm = true;
        }
      } catch (e) { console.error(e); }
    }
    document.getElementById('tune-form').addEventListener('submit', async (e) => {
      e.preventDefault();
      const freq = parseFloat(document.getElementById('input-freq').value);
      const program = parseInt(document.getElementById('input-program').value, 10);
      await fetch('/api/tune', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ freq, program })
      });
      await refreshStatus();
    });
    refreshStatus();
    setInterval(refreshStatus, 2000);
  </script>
</body>
</html>
"""


logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)


def create_app(
    store: Optional[StateStore] = None,
    tuner: Optional[TunerManager] = None,
) -> FastAPI:
    state_store = store or StateStore()
    tuner_manager = tuner or TunerManager(store=state_store)

    @asynccontextmanager
    async def lifespan(app: FastAPI):
        app.state.store = state_store
        app.state.tuner = tuner_manager
        tuner_manager.start()
        try:
            yield
        finally:
            tuner_manager.stop()

    app = FastAPI(
        title="NRSC-5 HD Radio Controller API",
        version="1.0.0",
        lifespan=lifespan,
    )
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_methods=["*"],
        allow_headers=["*"],
    )
    app.state.store = state_store
    app.state.tuner = tuner_manager

    @app.get("/", response_class=HTMLResponse)
    async def index() -> HTMLResponse:
        return HTMLResponse(content=DASHBOARD_HTML)

    @app.get("/api/status")
    async def get_status(request: Request) -> Dict[str, Any]:
        stream_url = build_stream_url(request)
        return app.state.store.snapshot(stream_url=stream_url)

    @app.post("/api/tune")
    async def post_tune(request: Request) -> Dict[str, Any]:
        try:
            body = await request.json()
        except Exception as exc:
            raise HTTPException(status_code=400, detail="Invalid JSON payload.") from exc

        if not isinstance(body, dict):
            raise HTTPException(status_code=400, detail="JSON body must be an object.")

        freq, program = validate_tune_params(body.get("freq"), body.get("program", 0))
        app.state.tuner.request_tune(freq=freq, program=program)
        return app.state.store.snapshot(stream_url=build_stream_url(request))

    @app.get("/api/tune")
    async def get_tune(
        request: Request,
        freq: Optional[float] = Query(default=None),
        program: Optional[int] = Query(default=0),
    ) -> Dict[str, Any]:
        freq_val, prog_val = validate_tune_params(freq, program)
        app.state.tuner.request_tune(freq=freq_val, program=prog_val)
        return app.state.store.snapshot(stream_url=build_stream_url(request))

    @app.get("/api/health")
    async def get_health() -> Dict[str, Any]:
        t: TunerManager = app.state.tuner
        return {
            "ok": True,
            "nrsc5_running": t.is_nrsc5_running(),
            "ffmpeg_running": t.is_ffmpeg_running(),
            "device_present": t.is_device_present(),
        }

    return app


app = create_app()
