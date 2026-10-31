import fcntl
import logging
import os
import queue
import shutil
import signal
import subprocess
import threading
import time
from pathlib import Path
from typing import List, Optional

from app.enricher import MetadataEnricher
from app.parser import parse_nrsc5_line
from app.state import StateStore

logger = logging.getLogger(__name__)

SAMPLE_RATE = 44100
CHANNELS = 2
BYTES_PER_SAMPLE = 2  # s16le
BYTES_PER_SECOND = SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE  # 176,400 bytes/sec
CHUNK_DURATION_SEC = 0.02  # 20ms chunks
CHUNK_BYTES = int(BYTES_PER_SECOND * CHUNK_DURATION_SEC)  # 3,528 bytes
SILENCE_CHUNK = b"\x00" * CHUNK_BYTES

# Linux ioctl USBDEVFS_RESET = _IO('U', 20) = 0x5514
USBDEVFS_RESET = 0x5514


def strip_wav_header_if_present(data: bytes) -> bytes:
    """If data starts with a 44-byte RIFF/WAVE header, strip it so raw PCM remains."""
    if len(data) >= 44 and data[:4] == b"RIFF" and data[8:12] == b"WAVE":
        return data[44:]
    return data


def prepare_rtlsdr_usb_device(reset_usb: bool = False) -> None:
    """Unbind any kernel DVB drivers (e.g. dvb_usb_rtl28xxu) holding the RTL-SDR
    interface and optionally issue a USBDEVFS_RESET ioctl so libusb can claim it.
    """
    sys_usb = Path("/sys/bus/usb/devices")
    if not sys_usb.exists():
        return

    try:
        for dev_dir in sys_usb.iterdir():
            vid_file = dev_dir / "idVendor"
            if not vid_file.exists():
                continue
            vid = vid_file.read_text(encoding="utf-8").strip().lower()
            if vid != "0bda":
                continue

            # Unbind kernel drivers from all interfaces of this Realtek USB device (e.g. 1-1:1.0, 1-1:1.1)
            dev_name = dev_dir.name
            for iface_dir in sys_usb.glob(f"{dev_name}:*"):
                driver_link = iface_dir / "driver"
                if driver_link.exists():
                    try:
                        driver_name = driver_link.resolve().name
                    except Exception:
                        driver_name = ""
                    # Unbind kernel DVB/radio drivers (leave usbfs alone unless resetting)
                    if driver_name and driver_name != "usbfs":
                        unbind_file = driver_link / "unbind"
                        if unbind_file.exists():
                            logger.info(
                                "Unbinding kernel driver '%s' from RTL-SDR interface %s",
                                driver_name,
                                iface_dir.name,
                            )
                            try:
                                unbind_file.write_text(iface_dir.name, encoding="utf-8")
                            except Exception as exc:
                                logger.warning("Failed to unbind %s: %s", iface_dir.name, exc)

            # Optionally perform a USB port reset on /dev/bus/usb/<busnum>/<devnum>
            if reset_usb:
                busnum_file = dev_dir / "busnum"
                devnum_file = dev_dir / "devnum"
                if busnum_file.exists() and devnum_file.exists():
                    busnum = int(busnum_file.read_text(encoding="utf-8").strip())
                    devnum = int(devnum_file.read_text(encoding="utf-8").strip())
                    dev_node = Path(f"/dev/bus/usb/{busnum:03d}/{devnum:03d}")
                    if dev_node.exists():
                        logger.info("Resetting RTL-SDR USB device node %s", dev_node)
                        try:
                            fd = os.open(str(dev_node), os.O_WRONLY)
                            try:
                                fcntl.ioctl(fd, USBDEVFS_RESET, 0)
                            finally:
                                os.close(fd)
                            time.sleep(0.4)
                        except Exception as exc:
                            logger.warning("USBDEVFS_RESET failed on %s: %s", dev_node, exc)
    except Exception as exc:
        logger.debug("Error inspecting /sys/bus/usb/devices: %s", exc)


class TunerManager:
    """Manages the persistent ffmpeg Icecast source and the retunable nrsc5 process."""

    def __init__(
        self,
        store: StateStore,
        debounce_sec: float = 0.3,
        enable_processes: Optional[bool] = None,
    ) -> None:
        self.store = store
        self.debounce_sec = debounce_sec
        if enable_processes is None:
            enable_processes = os.environ.get("DISABLE_TUNER_PROCESSES", "0") != "1"
        self.enable_processes = enable_processes

        self._running = False
        self._proc_lock = threading.Lock()
        self._spawn_lock = threading.Lock()
        self._tune_lock = threading.Lock()
        self._spawning_nrsc5 = False
        self._needs_usb_reset = False

        self._ffmpeg_proc: Optional[subprocess.Popen] = None
        self._nrsc5_proc: Optional[subprocess.Popen] = None
        self._nrsc5_generation = 0

        self._debounce_timer: Optional[threading.Timer] = None
        self._pending_target: Optional[tuple[float, int]] = None

        # Bounded queue of 20ms PCM chunks (~0.5s buffer max to keep switching snappy)
        self._pcm_queue: "queue.Queue[bytes]" = queue.Queue(maxsize=25)
        self._pacer_thread: Optional[threading.Thread] = None
        self._last_audio_time: float = 0.0
        self._tune_started_time: float = time.monotonic()
        self.enricher = MetadataEnricher()

    def build_nrsc5_cmd(self, freq: float, program: int) -> List[str]:
        cmd = ["nrsc5"]
        gain = os.environ.get("RTL_GAIN", "").strip()
        if gain:
            cmd.extend(["-g", gain])

        ppm = os.environ.get("RTL_PPM", "").strip()
        if ppm:
            cmd.extend(["-p", ppm])

        dev_idx = os.environ.get("RTL_DEVICE_INDEX", "0").strip()
        if dev_idx and dev_idx != "0":
            cmd.extend(["-d", dev_idx])

        # -o - outputs WAV to stdout (strip_wav_header_if_present strips the 44-byte header)
        cmd.extend(["-o", "-", f"{freq:.1f}", str(program)])
        return cmd

    def build_ffmpeg_cmd(self) -> List[str]:
        icecast_host = os.environ.get("ICECAST_HOST", "icecast").strip() or "icecast"
        icecast_port = os.environ.get("ICECAST_PORT", "8000").strip() or "8000"
        icecast_pw = os.environ.get("ICECAST_SOURCE_PASSWORD", "nrsc5_internal").strip() or "nrsc5_internal"
        mount = os.environ.get("ICECAST_MOUNT", "/hdradio").strip() or "/hdradio"
        if not mount.startswith("/"):
            mount = "/" + mount
        bitrate = os.environ.get("MP3_BITRATE", "192k").strip() or "192k"

        icecast_url = f"icecast://source:{icecast_pw}@{icecast_host}:{icecast_port}{mount}"
        return [
            "ffmpeg",
            "-hide_banner",
            "-loglevel",
            "warning",
            "-f",
            "s16le",
            "-ar",
            str(SAMPLE_RATE),
            "-ac",
            str(CHANNELS),
            "-i",
            "pipe:0",
            "-c:a",
            "libmp3lame",
            "-b:a",
            bitrate,
            "-content_type",
            "audio/mpeg",
            "-ice_name",
            "NRSC-5 HD Radio",
            "-f",
            "mp3",
            icecast_url,
        ]

    def start(self) -> None:
        if self._running:
            return
        self._running = True
        if not self.enable_processes:
            logger.info("TunerManager started with subprocesses disabled (test mode).")
            return

        freq, program = self.store.get_freq_program()
        self._spawn_nrsc5(freq, program)
        self.enricher.maybe_enrich(self.store)

        self._pacer_thread = threading.Thread(
            target=self._audio_pacer_loop, name="ffmpeg-audio-pacer", daemon=True
        )
        self._pacer_thread.start()

    def stop(self) -> None:
        self._running = False
        with self._tune_lock:
            if self._debounce_timer is not None:
                self._debounce_timer.cancel()
                self._debounce_timer = None
        self._terminate_nrsc5()
        self._terminate_ffmpeg()
        if self._pacer_thread is not None and self._pacer_thread.is_alive():
            self._pacer_thread.join(timeout=2.0)

    def request_tune(self, freq: float, program: int) -> None:
        """Update state immediately and schedule debounced nrsc5 restart."""
        freq = round(float(freq), 1)
        program = int(program)
        self.store.reset_for_tune(freq, program)
        self._last_audio_time = 0.0
        self._flush_pcm_queue()
        self._terminate_ffmpeg()

        if not self.enable_processes:
            return

        with self._tune_lock:
            self._pending_target = (freq, program)
            if self._debounce_timer is not None:
                self._debounce_timer.cancel()
            if self.debounce_sec <= 0:
                self._execute_pending_tune()
            else:
                self._debounce_timer = threading.Timer(
                    self.debounce_sec, self._execute_pending_tune
                )
                self._debounce_timer.daemon = True
                self._debounce_timer.start()

    def _execute_pending_tune(self) -> None:
        with self._tune_lock:
            target = self._pending_target
            self._pending_target = None
            self._debounce_timer = None
        if target is None or not self._running:
            return
        freq, program = target
        self._spawn_nrsc5(freq, program)
        self.enricher.maybe_enrich(self.store)

    def _flush_pcm_queue(self) -> None:
        while True:
            try:
                self._pcm_queue.get_nowait()
            except queue.Empty:
                break

    def _ensure_ffmpeg_running(self) -> Optional[subprocess.Popen]:
        with self._proc_lock:
            if self._ffmpeg_proc is not None and self._ffmpeg_proc.poll() is None:
                return self._ffmpeg_proc

            cmd = self.build_ffmpeg_cmd()
            try:
                logger.info("Starting ffmpeg Icecast encoder: %s", " ".join(cmd[:-1]))
                self._ffmpeg_proc = subprocess.Popen(
                    cmd,
                    stdin=subprocess.PIPE,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.PIPE,
                    bufsize=0,
                )
                threading.Thread(
                    target=self._drain_ffmpeg_stderr,
                    args=(self._ffmpeg_proc,),
                    daemon=True,
                ).start()
                return self._ffmpeg_proc
            except FileNotFoundError:
                logger.error("ffmpeg binary not found in PATH")
                return None
            except Exception as exc:
                logger.error("Failed to start ffmpeg: %s", exc)
                return None

    def _drain_ffmpeg_stderr(self, proc: subprocess.Popen) -> None:
        if proc.stderr is None:
            return
        try:
            for raw_line in iter(proc.stderr.readline, b""):
                if not raw_line:
                    break
                line = raw_line.decode("utf-8", errors="replace").strip()
                if line:
                    logger.warning("[ffmpeg] %s", line)
        except Exception:
            pass

    def _terminate_ffmpeg(self) -> None:
        with self._proc_lock:
            proc = self._ffmpeg_proc
            self._ffmpeg_proc = None
        if proc is not None:
            self._stop_process(proc, "ffmpeg")

    def _spawn_nrsc5(self, freq: float, program: int) -> None:
        with self._spawn_lock:
            self._spawning_nrsc5 = True
            try:
                had_previous = self._terminate_nrsc5()
                self._terminate_ffmpeg()
                self._flush_pcm_queue()
                self._last_audio_time = 0.0
                if had_previous:
                    time.sleep(0.35)

                if not self._running:
                    return

                # Ensure kernel DVB drivers are unbound (and reset USB port if previous claim failed)
                do_reset = self._needs_usb_reset
                self._needs_usb_reset = False
                prepare_rtlsdr_usb_device(reset_usb=do_reset)

                with self._proc_lock:
                    self._nrsc5_generation += 1
                    gen = self._nrsc5_generation
                    self._tune_started_time = time.monotonic()

                    cmd = self.build_nrsc5_cmd(freq, program)
                    try:
                        logger.info("Spawning nrsc5 (gen %d): %s", gen, " ".join(cmd))
                        proc = subprocess.Popen(
                            cmd,
                            stdin=subprocess.PIPE,
                            stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE,
                            bufsize=0,
                        )
                        self._nrsc5_proc = proc
                    except FileNotFoundError:
                        logger.error("nrsc5 binary not found in PATH")
                        self.store.update(state="error", synced=False)
                        return
                    except Exception as exc:
                        logger.error("Failed to spawn nrsc5: %s", exc)
                        self.store.update(state="error", synced=False)
                        return

                threading.Thread(
                    target=self._read_nrsc5_stdout,
                    args=(proc, gen),
                    name=f"nrsc5-stdout-{gen}",
                    daemon=True,
                ).start()
                threading.Thread(
                    target=self._read_nrsc5_stderr,
                    args=(proc, gen),
                    name=f"nrsc5-stderr-{gen}",
                    daemon=True,
                ).start()
            finally:
                self._spawning_nrsc5 = False

    def _terminate_nrsc5(self) -> bool:
        with self._proc_lock:
            proc = self._nrsc5_proc
            self._nrsc5_proc = None
        if proc is not None:
            self._stop_process(proc, "nrsc5", is_nrsc5=True)
            return True
        return False

    @staticmethod
    def _stop_process(proc: subprocess.Popen, name: str, is_nrsc5: bool = False) -> None:
        if proc.poll() is not None:
            return
        try:
            if is_nrsc5 and proc.stdin is not None:
                # nrsc5 polls stdin for 'q' to exit its main loop and call rtlsdr_close() cleanly
                try:
                    proc.stdin.write(b"q\n")
                    proc.stdin.flush()
                    proc.stdin.close()
                except Exception:
                    pass
                try:
                    proc.wait(timeout=1.2)
                    return
                except subprocess.TimeoutExpired:
                    pass

            if is_nrsc5:
                proc.send_signal(signal.SIGINT)
            else:
                proc.terminate()
            proc.wait(timeout=1.5)
        except subprocess.TimeoutExpired:
            try:
                proc.terminate()
                proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                logger.warning("%s did not terminate; killing", name)
                try:
                    proc.kill()
                    proc.wait(timeout=1.0)
                except Exception:
                    pass
        except Exception as exc:
            logger.debug("Error stopping %s: %s", name, exc)

    def _read_nrsc5_stdout(self, proc: subprocess.Popen, gen: int) -> None:
        if proc.stdout is None:
            return
        first_chunk = True
        leftover = b""
        try:
            while self._running and proc.poll() is None:
                data = proc.stdout.read(CHUNK_BYTES - len(leftover))
                if not data:
                    break
                if gen != self._nrsc5_generation:
                    break
                if first_chunk:
                    first_chunk = False
                    data = strip_wav_header_if_present(data)
                buf = leftover + data
                while len(buf) >= CHUNK_BYTES:
                    chunk = buf[:CHUNK_BYTES]
                    buf = buf[CHUNK_BYTES:]
                    self._last_audio_time = time.monotonic()
                    if self.store._state.state in ("tuning", "no_signal"):
                        self.store.update(state="playing", synced=True)
                    try:
                        self._pcm_queue.put_nowait(chunk)
                    except queue.Full:
                        try:
                            self._pcm_queue.get_nowait()
                        except queue.Empty:
                            pass
                        try:
                            self._pcm_queue.put_nowait(chunk)
                        except queue.Full:
                            pass
                leftover = buf
        except Exception as exc:
            logger.debug("nrsc5 stdout reader ended: %s", exc)

    def _read_nrsc5_stderr(self, proc: subprocess.Popen, gen: int) -> None:
        if proc.stderr is None:
            return
        try:
            for raw_line in iter(proc.stderr.readline, b""):
                if not raw_line:
                    break
                if gen != self._nrsc5_generation:
                    break
                line = raw_line.decode("utf-8", errors="replace").strip()
                if not line:
                    continue
                prev_title = self.store._state.title
                prev_artist = self.store._state.artist
                prev_station = self.store._state.station_name
                prev_synced = self.store._state.synced

                parsed = parse_nrsc5_line(line, self.store)
                lower = line.lower()

                if "usb_claim_interface error" in lower:
                    logger.error("[nrsc5] %s", line)
                    # Schedule a USBDEVFS_RESET before the next spawn so the busy interface is cleared
                    self._needs_usb_reset = True
                    self.store.update(state="error", synced=False)
                elif "no device found" in lower or "usb_open error" in lower or "failed to open rtlsdr" in lower:
                    logger.error("[nrsc5] %s", line)
                    self.store.update(state="error", synced=False)
                elif "synced" in parsed and parsed["synced"] != prev_synced:
                    logger.info("[nrsc5] %s", line)
                    if not parsed["synced"]:
                        logger.info("Lost synchronization: stopping Icecast stream")
                        self._terminate_ffmpeg()
                        self._flush_pcm_queue()
                elif "station_name" in parsed and parsed["station_name"] != prev_station:
                    logger.info("[nrsc5] %s", line)
                elif "title" in parsed and parsed["title"] and parsed["title"] != prev_title:
                    logger.info("[nrsc5] Now Playing: %s", parsed["title"])
                    self.enricher.maybe_enrich(self.store)
                elif "artist" in parsed and parsed["artist"] and parsed["artist"] != prev_artist:
                    logger.info("[nrsc5] Artist: %s", parsed["artist"])
                    self.enricher.maybe_enrich(self.store)
                else:
                    logger.debug("[nrsc5] %s", line)
        except Exception as exc:
            logger.debug("nrsc5 stderr reader ended: %s", exc)
        finally:
            rc = proc.poll()
            if self._running and gen == self._nrsc5_generation:
                logger.warning("nrsc5 process (gen %d) exited unexpectedly with code %s", gen, rc)
                if self.store._state.state != "error":
                    self.store.update(state="no_signal", synced=False)
                self._terminate_ffmpeg()
                self._flush_pcm_queue()

    def _audio_pacer_loop(self) -> None:
        """Continuously feed 20ms s16le chunks to ffmpeg at real-time pace when synchronized."""
        next_tick = time.monotonic()
        last_nrsc5_restart_attempt = time.monotonic()
        last_enrich_poll = time.monotonic()

        while self._running:
            now = time.monotonic()
            if now < next_tick:
                time.sleep(next_tick - now)
            next_tick += CHUNK_DURATION_SEC
            if time.monotonic() - next_tick > 0.2:
                next_tick = time.monotonic()

            # Periodically poll enhanced metadata (e.g. CPR Classical playlist) every 30s
            if now - last_enrich_poll > 30.0:
                last_enrich_poll = now
                self.enricher.maybe_enrich(self.store)

            is_synced = self.store._state.synced

            # Audio dropout detection: if marked synced but no audio has arrived for > 1.8 seconds, drop sync
            if is_synced and self._last_audio_time > 0 and (now - self._last_audio_time > 1.8):
                logger.info("Audio dropout detected (>1.8s); marking sync lost and stopping stream")
                self.store.update(synced=False, state="no_signal")
                is_synced = False
                self._terminate_ffmpeg()
                self._flush_pcm_queue()

            if not is_synced:
                # Stop ffmpeg so Icecast drops the stream instead of streaming silence
                if self.is_ffmpeg_running():
                    self._terminate_ffmpeg()
                    self._flush_pcm_queue()

                elapsed_since_tune = now - self._tune_started_time
                if (
                    self.store._state.state == "tuning"
                    and elapsed_since_tune > 12.0
                ):
                    self.store.update(state="no_signal")

                # Auto-restart nrsc5 only if it crashed and we aren't mid-tune/spawn
                if (
                    not self._spawning_nrsc5
                    and self._pending_target is None
                    and not self.is_nrsc5_running()
                    and now - last_nrsc5_restart_attempt > 3.0
                ):
                    last_nrsc5_restart_attempt = now
                    freq, program = self.store.get_freq_program()
                    threading.Thread(
                        target=self._spawn_nrsc5,
                        args=(freq, program),
                        daemon=True,
                    ).start()
                continue

            try:
                chunk = self._pcm_queue.get_nowait()
            except queue.Empty:
                chunk = SILENCE_CHUNK

            proc = self._ensure_ffmpeg_running()
            if proc is None or proc.stdin is None:
                time.sleep(0.1)
                next_tick = time.monotonic()
                continue

            try:
                proc.stdin.write(chunk)
            except (BrokenPipeError, OSError) as exc:
                logger.warning("ffmpeg stdin write failed (%s); restarting ffmpeg", exc)
                self._terminate_ffmpeg()
                next_tick = time.monotonic()

            # Auto-restart nrsc5 only if it crashed and we aren't mid-tune/spawn
            if (
                not self._spawning_nrsc5
                and self._pending_target is None
                and not self.is_nrsc5_running()
                and now - last_nrsc5_restart_attempt > 3.0
            ):
                last_nrsc5_restart_attempt = now
                freq, program = self.store.get_freq_program()
                threading.Thread(
                    target=self._spawn_nrsc5,
                    args=(freq, program),
                    daemon=True,
                ).start()

    def is_nrsc5_running(self) -> bool:
        with self._proc_lock:
            return self._nrsc5_proc is not None and self._nrsc5_proc.poll() is None

    def is_ffmpeg_running(self) -> bool:
        with self._proc_lock:
            return self._ffmpeg_proc is not None and self._ffmpeg_proc.poll() is None

    @staticmethod
    def is_device_present() -> bool:
        """Check whether an RTL-SDR / USB device is accessible in the container."""
        sys_usb = Path("/sys/bus/usb/devices")
        if sys_usb.exists():
            try:
                for dev_dir in sys_usb.iterdir():
                    vid_file = dev_dir / "idVendor"
                    if vid_file.exists():
                        vid = vid_file.read_text(encoding="utf-8").strip().lower()
                        if vid == "0bda":
                            return True
            except Exception:
                pass

        dev_usb = Path("/dev/bus/usb")
        if dev_usb.exists():
            try:
                for bus in dev_usb.iterdir():
                    if bus.is_dir():
                        for _ in bus.iterdir():
                            return True
            except Exception:
                pass

        return shutil.which("nrsc5") is not None and dev_usb.exists()
