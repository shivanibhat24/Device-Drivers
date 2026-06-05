#pragma once
#include <Arduino.h>

// ─── Intent types ────────────────────────────────────────────
enum CaptureIntent {
    INTENT_UNKNOWN = 0,
    INTENT_SINGLE,           // take one shot
    INTENT_BURST,            // rapid burst
    INTENT_TIMED_SEQUENCE,   // N shots at fixed interval
    INTENT_CONTINUOUS,       // capture until stop signal
};

// ─── Filter presets ──────────────────────────────────────────
enum FilterPreset {
    FILTER_NONE = 0,
    FILTER_GRAYSCALE,
    FILTER_SEPIA,
    FILTER_CINEMATIC,
    FILTER_VINTAGE,
    FILTER_WARM,
    FILTER_COOL,
    FILTER_BRIGHTNESS,
};

// ─── Structured command payload ──────────────────────────────
struct CommandPayload {
    CaptureIntent intent      = INTENT_SINGLE;
    int           count       = 1;
    unsigned long delay_ms    = 0;
    unsigned long interval_ms = 0;
    FilterPreset  filter      = FILTER_NONE;
    int           brightness  = 0;   // -127…+127  (for FILTER_BRIGHTNESS)
    bool          stop_on_cmd = false;
    bool          valid       = false;
};

// ─── API ─────────────────────────────────────────────────────
/**
 * Parse a raw JSON string (from HTTP body) into a CommandPayload.
 * Returns a payload with valid=true on success.
 */
CommandPayload parse_command(const String& json_body);

const char* intent_name(CaptureIntent i);
