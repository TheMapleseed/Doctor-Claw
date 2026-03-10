/**
 * Loop guard — circuit breaker for agent tool-call loops (OpenFang-inspired).
 */

#include "loop_guard.h"
#include <string.h>
#include <stdio.h>

static char g_history[LOOP_GUARD_HISTORY_LEN][64];
static size_t g_count = 0;
static bool g_tripped = false;
static char g_reason[128] = {0};

void loop_guard_reset(void) {
    g_count = 0;
    g_tripped = false;
    g_reason[0] = '\0';
}

static bool is_pingpong(void) {
    if (g_count < LOOP_GUARD_PINGPONG_LEN) return false;
    const char *a = g_history[g_count - 1];
    const char *b = g_history[g_count - 2];
    if (strcmp(a, b) == 0) return false;
    for (size_t i = 0; i + 2 <= g_count && g_count - i >= LOOP_GUARD_PINGPONG_LEN; i++) {
        size_t idx = g_count - 1 - i;
        if ((i % 2 == 0 && strcmp(g_history[idx], a) != 0) ||
            (i % 2 == 1 && strcmp(g_history[idx], b) != 0))
            return false;
    }
    return true;
}

static bool same_tool_repeated(void) {
    if (g_count < LOOP_GUARD_SAME_THRESH) return false;
    const char *last = g_history[g_count - 1];
    for (size_t i = 1; i < LOOP_GUARD_SAME_THRESH; i++) {
        if (strcmp(g_history[g_count - 1 - i], last) != 0) return false;
    }
    return true;
}

bool loop_guard_check(const char *tool_name) {
    if (g_tripped) return false;
    if (!tool_name) return true;
    if (g_count >= LOOP_GUARD_HISTORY_LEN) {
        memmove(&g_history[0], &g_history[1], (LOOP_GUARD_HISTORY_LEN - 1) * sizeof(g_history[0]));
        g_count = LOOP_GUARD_HISTORY_LEN - 1;
    }
    snprintf(g_history[g_count], sizeof(g_history[0]), "%s", tool_name);
    g_count++;
    if (same_tool_repeated()) {
        g_tripped = true;
        snprintf(g_reason, sizeof(g_reason), "same tool '%s' %d times in a row", tool_name, LOOP_GUARD_SAME_THRESH);
        return false;
    }
    if (is_pingpong()) {
        g_tripped = true;
        snprintf(g_reason, sizeof(g_reason), "ping-pong loop between '%s' and '%s'", g_history[g_count-2], g_history[g_count-1]);
        return false;
    }
    return true;
}

bool loop_guard_tripped(void) {
    return g_tripped;
}

void loop_guard_reason(char *buf, size_t buf_size) {
    if (!buf || buf_size == 0) return;
    snprintf(buf, buf_size, "%s", g_reason);
}
