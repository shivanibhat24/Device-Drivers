#include "command_parser.h"
#include "../system/config.h"
#include "../filters/filter_engine.h"
#include <ArduinoJson.h>

// ─── String → enum helpers ───────────────────────────────────
static CaptureIntent parse_intent(const char* s) {
    if (!s) return INTENT_UNKNOWN;
    String v(s); v.toLowerCase();
    if (v == "single"          || v == "capture_single")    return INTENT_SINGLE;
    if (v == "burst"           || v == "burst_capture"  )   return INTENT_BURST;
    if (v == "timed_sequence"  || v == "capture_sequence")  return INTENT_TIMED_SEQUENCE;
    if (v == "continuous"      || v == "capture_continuous") return INTENT_CONTINUOUS;
    return INTENT_UNKNOWN;
}

static FilterPreset parse_filter(const char* s) {
    if (!s) return FILTER_NONE;
    String v(s); v.toLowerCase();
    if (v.indexOf("gray")     >= 0) return FILTER_GRAYSCALE;
    if (v.indexOf("sepia")    >= 0) return FILTER_SEPIA;
    if (v.indexOf("cinematic")>= 0) return FILTER_CINEMATIC;
    if (v.indexOf("vintage")  >= 0) return FILTER_VINTAGE;
    if (v.indexOf("warm")     >= 0) return FILTER_WARM;
    if (v.indexOf("cool")     >= 0) return FILTER_COOL;
    if (v.indexOf("bright")   >= 0) return FILTER_BRIGHTNESS;
    return FILTER_NONE;
}

// ─── Main parser ─────────────────────────────────────────────
CommandPayload parse_command(const String& json_body) {
    CommandPayload cmd;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json_body);
    if (err) {
        Serial.printf("[PARSE] JSON error: %s\n", err.c_str());
        return cmd;   // valid = false
    }

    cmd.intent      = parse_intent(doc["intent"] | "single");
    cmd.count       = doc["count"]       | 1;
    cmd.delay_ms    = doc["delay_ms"]    | 0UL;
    cmd.interval_ms = doc["interval_ms"] | 0UL;
    cmd.filter      = parse_filter(doc["filter"] | "none");
    cmd.brightness  = doc["brightness"]  | 0;

    // Clamp to safe limits
    if (cmd.count    > MAX_BURST_COUNT)  cmd.count    = MAX_BURST_COUNT;
    if (cmd.delay_ms > MAX_DELAY_MS)     cmd.delay_ms = MAX_DELAY_MS;
    if (cmd.interval_ms > MAX_INTERVAL_MS) cmd.interval_ms = MAX_INTERVAL_MS;

    cmd.valid = (cmd.intent != INTENT_UNKNOWN);

#if DEBUG_VERBOSE
    Serial.printf("[PARSE] OK — intent=%s count=%d delay=%lu interval=%lu filter=%s\n",
        intent_name(cmd.intent), cmd.count, cmd.delay_ms, cmd.interval_ms,
        filter_name(cmd.filter));
#endif
    return cmd;
}

const char* intent_name(CaptureIntent i) {
    switch (i) {
        case INTENT_SINGLE:          return "single";
        case INTENT_BURST:           return "burst";
        case INTENT_TIMED_SEQUENCE:  return "timed_sequence";
        case INTENT_CONTINUOUS:      return "continuous";
        default:                     return "unknown";
    }
}
