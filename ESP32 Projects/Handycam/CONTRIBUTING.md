# Contributing

Bug reports, feature requests, and pull requests are welcome.

## Reporting issues

Include:
- Module variant (AI-Thinker, M5Camera, etc.)
- Arduino IDE and ESP32 board package versions
- Serial Monitor output at the point of failure
- The exact JSON command sent

## Pull requests

1. Fork → feature branch → PR against `main`
2. Keep firmware changes buildable with Arduino IDE (no PlatformIO-only features)
3. New filters go in `src/filters/` — add the enum entry to `command_parser.h` and a case to `filter_engine.cpp`
4. New intents go in `src/execution/command_parser.{h,cpp}` and a case in `scheduler.cpp`
5. Update `CHANGELOG.md` under `[Unreleased]`

## Code style

- 4-space indentation, no tabs
- `snake_case` for functions and variables
- All public headers include a one-line doc comment per function
- `Serial.printf` for debug output, gated on `DEBUG_VERBOSE`
