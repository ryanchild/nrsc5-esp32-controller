# ESP32-S3 CrowPanel 5.79" E-Paper Firmware

Custom PlatformIO firmware for the **Elecrow CrowPanel ESP32 5.79" E-Paper HMI Display** (`ESP32-S3-WROOM-1-N8R8`, `792×272` monochrome e-paper driven by dual SSD1683 controllers).

## Screen Layout (`792 × 272`)

- **Upper Left**: Large Station Frequency (`90.3`), inverted `[HD1]` subchannel badge, Play/Pause status pill, station callsign & preset name, and 4-bar MER signal meter.
- **Upper Right**: Large NTP Clock (`14:20` / `2:20 PM`), Date (`THU OCT 29`), and Open-Meteo local weather (custom icon, current temperature, daily High/Low, and rain probability).
- **Bottom (Full 792px Width)**: Program Service Data (Song Title in bold 24pt font, Artist — Album, and station slogan/genre) spanning the full width of the screen, plus the controls legend bar along the bottom edge.

## E-Paper Refresh Behavior

1. **Cycling Stations (`MENU` button)**:
   - Immediately performs a **full refresh** showing the new station frequency and HD subchannel (`90.3 FM [HD1]`) with `"Tuning..."`, and sends `POST /api/tune` to the NAS.
2. **Every 5 Seconds (`GET /api/status`)**:
   - Polls the NAS for updated Program Service Data (song title, artist, album, station callsign, HD lock). Whenever the program data changes, performs a fast **partial refresh** to populate/update the screen.
3. **Every 1 Minute (Clock Tick)**:
   - Performs a **full refresh** when the minute changes to update the clock and clear any e-ink ghosting.

## Configuration (`include/config.h`)

All settings are compiled from `include/config.h` (copied automatically from `include/config.example.h` on first build, and gitignored so your credentials are never committed):

1. `WIFI_SSID` / `WIFI_PASSWORD`
2. `NRSC5_API_BASE`: URL of your Synology NAS `nrsc5` container (e.g. `"http://192.168.1.50:8751"`)
3. `MEDIA_PLAY_URL` / `MEDIA_PAUSE_URL` / `MEDIA_VOLUME_UP_URL` / `MEDIA_VOLUME_DOWN_URL`: HTTP endpoints for your audio system (such as `node-sonos-http-api`)
4. `STATION_PRESETS[]`: List of `{ "Display Name", frequency_mhz, hd_program_index }`
5. `WEATHER_LAT` / `WEATHER_LON` / `WEATHER_LABEL` / `WEATHER_UNITS_F`

## Building & Flashing

```bash
pio run
pio run -t upload
```
