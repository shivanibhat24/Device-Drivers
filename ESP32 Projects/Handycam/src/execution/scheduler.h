#pragma once
#include "command_parser.h"

struct ScheduleResult {
    int captured;
    int errors;
};

ScheduleResult scheduler_execute(const CommandPayload& cmd);
