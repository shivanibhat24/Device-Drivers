# Changelog

All notable changes to this project will be documented here.  
Format follows [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

---

## [1.0.0] — 2025-06-06

### Added
- Initial firmware release targeting AI-Thinker ESP32-CAM (OV2640)
- HTTP server with `/execute`, `/stop`, `/status` endpoints
- Command parser: JSON → `CommandPayload` via ArduinoJson 7.x
- Scheduler: `single`, `burst_capture`, `timed_sequence`, `continuous` modes
- Filter engine: `grayscale`, `sepia`, `cinematic`, `vintage`, `warm`, `cool`, `brightness`
- FSM with 8 states and Serial transition logging
- PSRAM-aware frame sizing (UXGA with PSRAM, SVGA without)
- Desktop NLP REPL (`handycam_nlp.py`) — regex-based, no API key required
- curl smoke test script (`test_api.sh`)
- Full HTTP protocol reference (`docs/protocol.md`)
