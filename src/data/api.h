#pragma once
#include <Arduino.h>
#include <time.h>
#include "storage.h"

struct ModelSlot {
    float pct = -1.0f;
    char  label[12] = "";
};

struct ClaudeData {
    float     sessionPct   = -1.0f;
    float     weeklyPct    = -1.0f;
    time_t    sessionReset = 0;
    time_t    weeklyReset  = 0;
    ModelSlot models[3];               // top model breakdowns
    bool      valid = false;
    char      err[24] = "";
    char      rawKeys[128] = "";           // debug: comma-separated API keys with utilization
};

namespace Api {
    // Authenticates and pulls the org's usage. Updates the ClaudeData passed in.
    bool fetchClaude(const Settings& s, ClaudeData& out);

    // Helpers for displaying countdowns.
    String formatCountdown(time_t t);
}
