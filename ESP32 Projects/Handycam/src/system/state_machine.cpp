#include "state_machine.h"
#include "../system/config.h"
#include "../core/camera_driver.h"
#include "../core/frame_capture.h"
#include "../execution/scheduler.h"
#include "../filters/filter_engine.h"

#include <Arduino.h>

// ─── Helpers ─────────────────────────────────────────────────
const char* state_name(HandycamState s) {
    switch (s) {
        case STATE_IDLE:             return "IDLE";
        case STATE_CMD_RECEIVED:     return "CMD_RECEIVED";
        case STATE_PARSE_INTENT:     return "PARSE_INTENT";
        case STATE_SCHEDULE_ACTION:  return "SCHEDULE_ACTION";
        case STATE_CAPTURE_SEQUENCE: return "CAPTURE_SEQUENCE";
        case STATE_APPLY_FILTER:     return "APPLY_FILTER";
        case STATE_SEND_RESULT:      return "SEND_RESULT";
        case STATE_ERROR:            return "ERROR";
        default:                     return "UNKNOWN";
    }
}

static void transition(HandycamState next) {
#if DEBUG_VERBOSE
    Serial.printf("[SM] %s → %s\n", state_name(g_state), state_name(next));
#endif
    g_state = next;
}

// ─── Public API ──────────────────────────────────────────────
void state_machine_init() {
    g_state = STATE_IDLE;
}

void state_machine_run(const CommandPayload& cmd) {

    // CMD_RECEIVED
    transition(STATE_CMD_RECEIVED);
    Serial.printf("[CMD] intent=%s count=%d delay=%lums interval=%lums filter=%s\n",
        intent_name(cmd.intent), cmd.count, cmd.delay_ms, cmd.interval_ms,
        filter_name(cmd.filter));

    // PARSE_INTENT — validation
    transition(STATE_PARSE_INTENT);
    if (cmd.intent == INTENT_UNKNOWN) {
        Serial.println(F("[ERR] Unknown intent — aborting"));
        transition(STATE_ERROR);
        transition(STATE_IDLE);
        return;
    }
    if (cmd.count < 1 || cmd.count > MAX_BURST_COUNT) {
        Serial.printf("[ERR] count %d out of range [1,%d]\n", cmd.count, MAX_BURST_COUNT);
        transition(STATE_ERROR);
        transition(STATE_IDLE);
        return;
    }

    // SCHEDULE_ACTION — honour initial delay
    transition(STATE_SCHEDULE_ACTION);
    if (cmd.delay_ms > 0) {
        Serial.printf("[SCHED] Waiting %lu ms before capture…\n", cmd.delay_ms);
        delay(cmd.delay_ms);
    }

    // CAPTURE_SEQUENCE
    transition(STATE_CAPTURE_SEQUENCE);
    ScheduleResult result = scheduler_execute(cmd);

    // APPLY_FILTER (post-capture, applied per-frame inside scheduler)
    transition(STATE_APPLY_FILTER);
    // Filter application is done inline in scheduler_execute();
    // this state is a logical marker for the state-machine log.

    // SEND_RESULT
    transition(STATE_SEND_RESULT);
    Serial.printf("[DONE] captured=%d errors=%d\n",
                  result.captured, result.errors);

    transition(STATE_IDLE);
}
