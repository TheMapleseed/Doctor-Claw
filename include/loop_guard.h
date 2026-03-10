#ifndef DOCTORCLAW_LOOP_GUARD_H
#define DOCTORCLAW_LOOP_GUARD_H

/**
 * Loop guard — OpenFang-style circuit breaker for agent tool-call loops.
 * Detects repeated or ping-pong tool calls and trips to prevent runaway loops.
 */

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>

#define LOOP_GUARD_HISTORY_LEN  16
#define LOOP_GUARD_SAME_THRESH  5   /* same tool N times in a row → trip */
#define LOOP_GUARD_PINGPONG_LEN 6   /* A,B,A,B,A,B → trip */

/** Reset state (e.g. at start of new task or conversation). */
void loop_guard_reset(void);

/**
 * Record a tool call and check for loop. Call before executing each tool.
 * Returns true if OK to proceed, false if circuit tripped (caller should stop).
 */
bool loop_guard_check(const char *tool_name);

/** Returns true if the last check tripped the guard. */
bool loop_guard_tripped(void);

/** Get a short reason string for why the guard tripped (for logging). */
void loop_guard_reason(char *buf, size_t buf_size);

#endif
