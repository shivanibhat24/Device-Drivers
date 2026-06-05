#include "scheduler.h"
#include "../core/frame_capture.h"
#include "../filters/filter_engine.h"
#include "../system/config.h"
#include <Arduino.h>

// External flag set by HTTP server when a STOP command arrives
extern volatile bool g_stop_requested;

// ─── Burst defaults ──────────────────────────────────────────
static const unsigned long BURST_INTERVAL_MS    = 150;
static const unsigned long SEQUENCE_INTERVAL_MS = 1500;

// ─── Internal ────────────────────────────────────────────────
static ScheduleResult _run_loop(const CommandPayload& cmd, unsigned long interval_ms) {
    ScheduleResult res = {0, 0};

    for (int i = 0; i < cmd.count; i++) {
        if (g_stop_requested) {
            Serial.println(F("[SCHED] Stop requested — aborting sequence"));
            g_stop_requested = false;
            break;
        }

        Serial.printf("[CAPTURE] frame %d/%d … ", i + 1, cmd.count);

        camera_fb_t* fb = capture_frame();
        if (!fb) {
            Serial.println(F("FAILED"));
            res.errors++;
        } else {
            // Apply filter in-place
            filter_apply(fb, cmd.filter, cmd.brightness);
            // In a real deployment: fb data streamed / saved to SD here
            Serial.printf("OK (%u bytes)\n", fb->len);
            esp_camera_fb_return(fb);
            res.captured++;
        }

        if (i < cmd.count - 1 && interval_ms > 0) {
            delay(interval_ms);
        }
    }
    return res;
}

// ─── Public ──────────────────────────────────────────────────
ScheduleResult scheduler_execute(const CommandPayload& cmd) {
    switch (cmd.intent) {

        case INTENT_SINGLE:
            return _run_loop(cmd, 0);

        case INTENT_BURST: {
            unsigned long iv = cmd.interval_ms > 0 ? cmd.interval_ms : BURST_INTERVAL_MS;
            return _run_loop(cmd, iv);
        }

        case INTENT_TIMED_SEQUENCE: {
            unsigned long iv = cmd.interval_ms > 0 ? cmd.interval_ms : SEQUENCE_INTERVAL_MS;
            return _run_loop(cmd, iv);
        }

        case INTENT_CONTINUOUS: {
            // Ignore cmd.count — run until stop signal
            CommandPayload loop_cmd = cmd;
            loop_cmd.count = MAX_BURST_COUNT;
            unsigned long iv = cmd.interval_ms > 0 ? cmd.interval_ms : SEQUENCE_INTERVAL_MS;
            return _run_loop(loop_cmd, iv);
        }

        default: {
            ScheduleResult r = {0, 1};
            return r;
        }
    }
}
