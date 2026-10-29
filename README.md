# nrsc5-esp32-controller

An end-to-end **HD Radio (NRSC-5) network streamer and e-paper desktop controller**:

1. **`nas-stack/` (Synology NAS / Linux Docker Stack)**:
   - Tunes over-the-air North American digital HD Radio broadcasts (`HD1`, `HD2`, `HD3`, etc.) using an **RTL-SDR USB dongle** and [`theori-io/nrsc5`](https://github.com/theori-io/nrsc5).
   - Maintains a **persistent, uninterrupted MP3 Icecast stream** (`/hdradio`) so network speakers (Sonos, Home Assistant, VLC, etc.) **never disconnect or drop playback** while switching stations.
   - Exposes a lightweight **FastAPI REST service** (`:8080`) for tuning channels, inspecting live Program Service Data (Title, Artist, Album, Station Callsign, MER/BER signal metrics), and a built-in web UI.
2. **`esp32-firmware/` (Elecrow CrowPanel ESP32-S3 5.79" E-Paper Controller)**:
   - Custom firmware for the **Elecrow CrowPanel 5.79" E-Paper HMI Display** (`792×272` monochrome, dual SSD1683 controllers, ESP32-S3-WROOM-1-N8R8).
   - Displays the current station, HD subchannel badge, signal bars, track title, artist, album, large digital clock, and local weather (via Open-Meteo).
   - Fully decoupled media control: hits user-configured HTTP endpoints (`MEDIA_PLAY_URL`, `MEDIA_PAUSE_URL`, volume URLs) defined at compile time in [`esp32-firmware/include/config.h`](esp32-firmware/include/config.example.h)—works out of the box with [`node-sonos-http-api`](https://github.com/jishi/node-sonos-http-api), Home Assistant webhooks, or any HTTP-controllable audio player.

---

## Architecture

```mermaid
flowchart LR
    subgraph NAS ["Synology NAS (Docker Compose)"]
        SDR["RTL-SDR Dongle<br/>(/dev/bus/usb)"] --> NRSC5["nrsc5 Decoder"]
        NRSC5 -- "Raw PCM" --> PUMP["Real-Time PCM Pump<br/>(Silence during retune)"]
        PUMP --> FFMPEG["FFmpeg<br/>(192k MP3)"]
        FFMPEG --> ICECAST["Icecast2<br/>(:8000/hdradio)"]
        NRSC5 -- "PSD Metadata & MER" --> API["FastAPI Server<br/>(:8080)"]
    end

    subgraph Audio ["Network Audio"]
        ICECAST -- "http://nas:8000/hdradio" --> SPEAKER["Sonos / Network Speaker"]
        SONOS_API["node-sonos-http-api<br/>(:5005)"] --> SPEAKER
    end

    subgraph ESP32 ["CrowPanel 5.79\" E-Paper (ESP32-S3)"]
        BTN_MENU["MENU Button (GPIO 2)"] -- "POST /api/tune" --> API
        BTN_EXIT["EXIT Button (GPIO 1)"] -- "Play / Pause URL" --> SONOS_API
        DIAL["3-Way Dial (GPIO 4/5/6)"] -- "Volume / Station" --> SONOS_API
        API -- "GET /api/status" --> UI["792x272 E-Ink Dashboard"]
        METEO["Open-Meteo API"] --> UI
        NTP["NTP Clock"] --> UI
    end
```

---

## Repository Structure

```text
nrsc5-esp32-controller/
├── docker-compose.yml              # Icecast + NRSC5 FastAPI container stack
├── .env.example                    # Environment variable template for NAS stack
├── nas-stack/
│   ├── Dockerfile                  # Multi-stage build compiling theori-io/nrsc5 + FastAPI
│   ├── requirements.txt            # Python dependencies
│   ├── app/
│   │   ├── main.py                 # FastAPI endpoints & web dashboard
│   │   ├── tuner.py                # nrsc5 process manager & continuous PCM->FFmpeg pump
│   │   ├── parser.py               # Real-time nrsc5 stderr parser (PSD, MER, BER, programs)
│   │   └── state.py                # State models & last-station persistence
│   └── tests/                      # Pytest suite for parser & REST endpoints
└── esp32-firmware/
    ├── platformio.ini              # PlatformIO config for ESP32-S3 N8R8 (8MB Flash/PSRAM)
    ├── scripts/ensure_config.py    # Pre-build hook copying config.example.h -> config.h
    ├── include/
    │   ├── config.example.h        # Compile-time configuration template
    │   └── icons.h                 # Custom vector/bitmap icons for weather & status
    └── src/
        ├── main.cpp                # Application loop & task coordination
        ├── display.{h,cpp}         # 792x272 GxEPD2 layout & partial/full refresh manager
        ├── input.{h,cpp}           # Debounced buttons & 3-way rotary switch handler
        ├── net.{h,cpp}             # Wi-Fi, NTP, NAS API, Media URLs & Open-Meteo client
        └── app_state.h             # Shared state definitions
```

---

## Part 1: Synology NAS Setup (`nas-stack`)

### 1. Plug in the RTL-SDR Dongle
1. Connect your RTL-SDR USB dongle (RTL2832U-based, such as RTL-SDR Blog V3/V4 or Nooelec) to a USB port on your Synology NAS.
2. **DSM 7+ Note**: Synology DSM 7 removed kernel DVB tuner drivers, which is actually ideal because `librtlsdr` accesses the dongle directly via userland `libusb` when `/dev/bus/usb` is passed into the container with `privileged: true`.
   - *Linux host note*: If running on a standard Linux distro (Ubuntu/Debian) where the kernel `dvb_usb_rtl28xxu` module claims the device, blacklist it on the host via `echo "blacklist dvb_usb_rtl28xxu" | sudo tee /etc/modprobe.d/blacklist-rtlsdr.conf`.

### 2. Configure & Launch with Docker Compose
```bash
cp .env.example .env
# Edit .env if you want to customize default frequency, bitrate, gain, or passwords
docker compose up -d --build
```

### 3. Verify Stream & API
- **Web Dashboard**: Open `http://<nas-ip>:8080/` in a browser to see live signal status and test tuning.
- **Audio Stream URL**: `http://<nas-ip>:8000/hdradio` (open in VLC or browser to test audio).
- **Status JSON**: `curl http://<nas-ip>:8080/api/status`
- **Tune Station**:
  ```bash
  # POST JSON
  curl -X POST http://<nas-ip>:8080/api/tune \
    -H "Content-Type: application/json" \
    -d '{"freq": 90.3, "program": 0}'

  # Or GET query parameters
  curl "http://<nas-ip>:8080/api/tune?freq=90.3&program=0"
  ```
  *Note: `program` is `0` for `HD1` (main channel), `1` for `HD2`, `2` for `HD3`, etc.*

### 4. Adding the Icecast Stream to Sonos
To play the HD Radio stream on Sonos:
1. Open the **Sonos App** (or use TuneIn / custom radio station URL, or `node-sonos-http-api`'s preset/clip/stream feature) and point it to:
   ```text
   http://<nas-ip>:8000/hdradio
   ```
2. Because `nas-stack` continuously feeds silent PCM frames into FFmpeg while `nrsc5` retunes between frequencies, the Icecast mount point `/hdradio` **never drops its TCP stream**. Once Sonos is playing the stream, you can freely pause/play Sonos and cycle stations on the SDR without Sonos disconnecting.

---

## Part 2: ESP32-S3 CrowPanel 5.79" Firmware (`esp32-firmware`)

### Hardware Overview
- **Board**: [Elecrow CrowPanel ESP32 5.79" E-Paper HMI Display](https://www.amazon.com/dp/B0FX4PDW6M) (`ESP32-S3-WROOM-1-N8R8`, `792×272` B/W e-paper, dual SSD1683).
- **Pin Mapping**:
  | Function | ESP32-S3 GPIO | Notes |
  | :--- | :--- | :--- |
  | E-Paper SPI SCK / MOSI | `GPIO 12` / `GPIO 11` | Explicit hardware SPI init |
  | E-Paper CS / DC / RST / BUSY | `GPIO 45` / `46` / `47` / `48` | `GxEPD2_579_GDEY0579T93` |
  | E-Paper Power Enable | `GPIO 7` | Driven `HIGH` before init |
  | **MENU** Button | `GPIO 2` | Short: Next Station · Long (1.5s): Full Refresh |
  | **EXIT** Button | `GPIO 1` | Play / Pause toggle |
  | **Rotary Switch UP / DOWN / CLICK** | `GPIO 6` / `GPIO 4` / `GPIO 5` | Volume +/- or Prev/Next Station (Click toggles mode) |

### 1. Configure `include/config.h`
Copy [`esp32-firmware/include/config.example.h`](esp32-firmware/include/config.example.h) to `esp32-firmware/include/config.h` (this file is gitignored so your credentials stay private):

```bash
cd esp32-firmware
cp include/config.example.h include/config.h
```

Edit `include/config.h` to set:
- **Wi-Fi credentials**: `WIFI_SSID` and `WIFI_PASSWORD`
- **NAS API URL**: `NRSC5_API_BASE` (e.g. `"http://192.168.1.50:8080"`)
- **Play / Pause / Volume URLs**:
  ```cpp
  #define MEDIA_PLAY_URL        "http://192.168.1.50:5005/Living%20Room/play"
  #define MEDIA_PAUSE_URL       "http://192.168.1.50:5005/Living%20Room/pause"
  #define MEDIA_PLAYPAUSE_URL   "http://192.168.1.50:5005/Living%20Room/playpause"
  #define MEDIA_STATE_URL       "http://192.168.1.50:5005/Living%20Room/state"
  #define MEDIA_VOLUME_UP_URL   "http://192.168.1.50:5005/Living%20Room/volume/+2"
  #define MEDIA_VOLUME_DOWN_URL "http://192.168.1.50:5005/Living%20Room/volume/-2"
  ```
- **Station Presets**:
  ```cpp
  static const StationPreset STATION_PRESETS[] = {
      { "KEXP 90.3 HD1",  90.3f,  0 },
      { "KUOW 94.9 HD1",  94.9f,  0 },
      { "KUOW 94.9 HD2",  94.9f,  1 },
      { "KNDD 107.7 HD1", 107.7f, 0 },
  };
  ```
- **Weather Location & Timezone**:
  - `WEATHER_LAT`, `WEATHER_LON`, `WEATHER_LABEL`, `WEATHER_UNITS_F`
  - `TZ_INFO` (POSIX TZ string with automatic DST calculation)

### 2. Build & Flash with PlatformIO
```bash
cd esp32-firmware
pio run -t upload
```

See [`esp32-firmware/README.md`](esp32-firmware/README.md) for detailed flashing, button controls, and display troubleshooting.

---

## REST API Reference (`nas-stack`)

| Method & Path | Description |
| :--- | :--- |
| `GET /api/status` | Returns JSON with `state` (`tuning`, `playing`, `no_signal`, `error`), `freq`, `program`, `station_name`, `callsign`, `slogan`, `title`, `artist`, `album`, `genre`, `programs`, `mer_db`, `ber`, `synced`, `stream_url`, and `updated_at`. |
| `POST /api/tune` | Body: `{"freq": 90.3, "program": 0}`. Retunes `nrsc5` to the requested frequency (87.5–108.0 MHz) and HD subchannel (`0`–`7`). |
| `GET /api/tune` | Query params: `?freq=90.3&program=0`. Same as `POST /api/tune` for convenient browser/curl testing. |
| `GET /api/health` | Returns `{"ok": true, "nrsc5_running": bool, "ffmpeg_running": bool, "device_present": bool}`. |
| `GET /` | Built-in HTML status page and manual tuner UI. |
