<div align="center">

# Handycam

**Natural Language–Controlled Smart Edge Camera System**

*Say what you want. The camera listens.*

[![Platform](https://img.shields.io/badge/platform-ESP32--CAM-blue?style=flat-square)](https://github.com/espressif/esp32-camera)
[![Framework](https://img.shields.io/badge/framework-Arduino-orange?style=flat-square)](https://docs.espressif.com)
[![Firmware](https://img.shields.io/badge/firmware-C%2B%2B17-green?style=flat-square)]()
[![NLP](https://img.shields.io/badge/NLP-Python%203-yellow?style=flat-square)]()
[![License](https://img.shields.io/badge/license-MIT-lightgrey?style=flat-square)](LICENSE)

</div>

---

Handycam replaces rigid button-based camera interfaces with conversational instructions. A mobile app or Python script converts voice into a structured JSON command; the ESP32-CAM executes it at the edge — timed bursts, filtered sequences, delayed triggers — with no cloud dependency.

```
"Take 5 cinematic shots after 10 seconds with a warm filter"
                          ↓ NLP layer
{
  "intent":      "timed_sequence",
  "count":        5,
  "delay_ms":     10000,
  "interval_ms":  1500,
  "filter":       "cinematic"
}
                          ↓ HTTP POST /execute
          ESP32-CAM schedules, captures, filters, returns status
```

---

## Contents

- [Architecture](#architecture)
- [Hardware](#hardware)
- [Wiring](#wiring)
- [Repository Layout](#repository-layout)
- [Getting Started](#getting-started)
- [HTTP API](#http-api)
- [Filter Presets](#filter-presets)
- [NLP Examples](#nlp-examples)
- [Roadmap](#roadmap)

---

## Architecture

```
MOBILE / HOST LAYER
  Microphone
      │
  Speech-to-Text
      │
  NLP Parser  ──────  handycam_nlp.py (Python) or mobile app
      │
  JSON Command
      │
      │  HTTP POST /execute
      ▼
ESP32-CAM EDGE LAYER
  WiFi Server
      │
  Command Parser  ──  ArduinoJson, intent + filter resolution
      │
  State Machine   ──  IDLE → CMD_RECEIVED → PARSE → SCHEDULE
      │                   → CAPTURE → FILTER → RESULT → IDLE
  Scheduler
      │
  Camera Pipeline ──  OV2640, JPEG, PSRAM-aware frame sizing
      │
  Filter Engine   ──  RGB888 per-pixel transform, re-encoded JPEG
      │
  HTTP Response   ──  status + frame count
```

---

## Hardware

| Component | Detail |
|-----------|--------|
| Module | AI-Thinker ESP32-CAM |
| MCU | ESP32-S, 240 MHz dual-core |
| Camera | OV2640, up to 2 MP |
| Flash | 4 MB |
| PSRAM | 4 MB (optional — enables UXGA capture; SVGA used otherwise) |
| Connectivity | 802.11 b/g/n WiFi |
| Programming | External FTDI (3.3 V) — no onboard USB |

---

## Wiring

The ESP32-CAM has no onboard USB. You need an **FTDI adapter at 3.3 V** to flash it.

```
FTDI Adapter          ESP32-CAM
──────────────────────────────────
3.3V  ──────────────► 3.3V
GND   ──────────────► GND
TX    ──────────────► U0R (GPIO3)
RX    ──────────────► U0T (GPIO1)
                      IO0 ──► GND   (hold during upload only)
```

> **Flash mode:** Bridge IO0 to GND before powering on, upload, then remove the bridge and press RST.

> **Power:** The AI-Thinker module can draw up to 300 mA during WiFi transmit. Use a dedicated 3.3 V supply, not the FTDI's 3.3 V rail, if you see resets.

---

## Repository Layout

```
Handycam/
├── Handycam.ino                    Entry point — setup, loop, global state
├── src/
│   ├── core/
│   │   ├── camera_driver.{h,cpp}   OV2640 init, AI-Thinker pinout, PSRAM detection
│   │   └── frame_capture.{h,cpp}   Single-frame JPEG acquisition
│   ├── execution/
│   │   ├── command_parser.{h,cpp}  JSON → CommandPayload (ArduinoJson 7.x)
│   │   └── scheduler.{h,cpp}       Timing engine: burst / sequence / continuous
│   ├── filters/
│   │   └── filter_engine.{h,cpp}   7 pixel-level presets, JPEG decode+re-encode
│   ├── comms/
│   │   └── wifi_server.{h,cpp}     WebServer: /execute /stop /status
│   └── system/
│       ├── config.h                Pin map, WiFi credentials, capture limits
│       └── state_machine.{h,cpp}   FSM with logged transitions
├── scripts/
│   ├── handycam_nlp.py             Desktop NLP REPL — no API key needed
│   └── test_api.sh                 curl smoke tests for all endpoints
├── docs/
│   └── protocol.md                 Full HTTP API reference
├── CHANGELOG.md
├── CONTRIBUTING.md
├── LICENSE
└── .gitignore
```

---

## Getting Started

### 1. Dependencies

In **Arduino IDE → Library Manager**, install:

```
ArduinoJson  ≥ 7.0
```

Add the ESP32 board package (if not already present):

```
https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
```

Board package: **esp32 by Espressif ≥ 3.0**  
Board selection: **AI Thinker ESP32-CAM**

### 2. Configure credentials

Edit `src/system/config.h`:

```cpp
#define WIFI_SSID     "your_ssid"
#define WIFI_PASSWORD "your_password"
```

Optional tuning:

```cpp
#define JPEG_QUALITY     12   // 0 = best, 63 = smallest
#define MAX_BURST_COUNT  50   // hard cap per command
```

### 3. Flash

1. Wire FTDI to ESP32-CAM per the [wiring diagram](#wiring) above
2. Bridge **IO0 → GND**
3. Power on, upload from Arduino IDE at **115200 baud**
4. Remove IO0 bridge, press **RST**
5. Open Serial Monitor — the device IP is printed on boot

### 4. Send a command

**curl — instant smoke test:**

```bash
curl -X POST http://<ESP32-IP>/execute \
  -H "Content-Type: application/json" \
  -d '{"intent":"burst_capture","count":5,"delay_ms":3000,"filter":"cinematic"}'
```

**Python NLP client:**

```bash
pip install requests
python scripts/handycam_nlp.py --ip 192.168.1.42

🎤  > Take 5 cinematic shots after 3 seconds with warm tones
→ Payload: {"intent":"timed_sequence","count":5,"delay_ms":3000,"filter":"cinematic"}
→ Response [202]: {"status":"accepted","intent":"timed_sequence","count":5}
```

**Run all smoke tests:**

```bash
chmod +x scripts/test_api.sh
./scripts/test_api.sh 192.168.1.42
```

---

## HTTP API

### `POST /execute`

Trigger a capture sequence.

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `intent` | string | see below | — | Capture mode |
| `count` | int | 1–50 | 1 | Number of frames |
| `delay_ms` | int | 0–60000 | 0 | Pre-capture delay (ms) |
| `interval_ms` | int | 0–10000 | mode default | Inter-shot gap (ms) |
| `filter` | string | see below | `none` | Post-processing preset |
| `brightness` | int | -127–+127 | 0 | Only used with `filter: brightness` |

**Intent values**

| Value | Behaviour |
|-------|-----------|
| `single` | One frame |
| `burst_capture` | Rapid-fire (default 150 ms interval) |
| `timed_sequence` | N frames at controlled interval (default 1500 ms) |
| `continuous` | Loop until `POST /stop` is received |

**Response codes**

| Code | Meaning |
|------|---------|
| 202 | Accepted and queued |
| 400 | Empty body |
| 409 | Device busy — sequence in progress |
| 422 | Command parsed but invalid |

### `POST /stop`

Abort the running sequence at the next frame boundary.

```json
{ "status": "stop_requested" }
```

### `GET /status`

Returns current FSM state.

```json
{ "state": "CAPTURE_SEQUENCE" }
```

Possible values: `IDLE`, `CMD_RECEIVED`, `PARSE_INTENT`, `SCHEDULE_ACTION`, `CAPTURE_SEQUENCE`, `APPLY_FILTER`, `SEND_RESULT`, `ERROR`

---

## Filter Presets

| Key | Description |
|-----|-------------|
| `none` | Pass-through |
| `grayscale` | Luminance desaturation (BT.601 coefficients) |
| `sepia` | Classic warm-brown matrix |
| `cinematic` | Contrast boost + warm grade + shadow desaturation |
| `vintage` | Sepia + reduced saturation + deterministic luminance noise |
| `warm` | R+20 / G+8 / B−20 |
| `cool` | R−20 / B+20 |
| `brightness` | Uniform offset, magnitude set by `brightness` field |

All filters decode the JPEG frame to RGB888, apply per-pixel transforms, and re-encode. With PSRAM, this is stable at SVGA (800×600). Without PSRAM, use QVGA or VGA — set `FRAMESIZE_QVGA` in `config.h`.

---

## NLP Examples

| Phrase | Parsed command |
|--------|----------------|
| "Take a photo" | `single`, count=1 |
| "Take 5 quick shots" | `burst_capture`, count=5 |
| "3 cinematic shots after 10 seconds" | `timed_sequence`, count=3, delay=10 s, filter=cinematic |
| "Capture slowly for 15 seconds" | `timed_sequence`, interval=3 s |
| "Keep shooting until I say stop" | `continuous` |
| "Sepia burst, 8 frames" | `burst_capture`, count=8, filter=sepia |
| "A few warm shots in 5 seconds" | `timed_sequence`, count=5, delay=5 s, filter=warm |

The `handycam_nlp.py` script handles these with regex rules — no API key required. For production NLP, swap in Whisper STT + an LLM call with structured output.

---

## Roadmap

- [ ] SD card image storage (slot already on the AI-Thinker module)
- [ ] WebSocket live preview stream
- [ ] BLE GATT command path (low-latency alternative to HTTP)
- [ ] On-device Whisper STT (ESP32-S3 + I2S microphone)
- [ ] Flutter mobile app with native STT and image gallery
- [ ] MQTT support for multi-camera orchestration
- [ ] Wake-word trigger

---

## License

MIT — see [LICENSE](LICENSE).
