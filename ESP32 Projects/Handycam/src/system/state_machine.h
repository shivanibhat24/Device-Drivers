#pragma once
#include "src/execution/command_parser.h"

/**
 * Handycam system states (matches architecture diagram).
 */
enum HandycamState {
    STATE_IDLE,
    STATE_CMD_RECEIVED,
    STATE_PARSE_INTENT,
    STATE_SCHEDULE_ACTION,
    STATE_CAPTURE_SEQUENCE,
    STATE_APPLY_FILTER,
    STATE_SEND_RESULT,
    STATE_ERROR
};

extern HandycamState g_state;

void state_machine_init();
void state_machine_run(const CommandPayload& cmd);
const char* state_name(HandycamState s);
