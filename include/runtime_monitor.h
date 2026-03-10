#ifndef DOCTORCLAW_RUNTIME_MONITOR_H
#define DOCTORCLAW_RUNTIME_MONITOR_H

/**
 * Runtime monitoring: when the system is running, security events, penetration
 * attempts, and errors are recorded and can notify both the user and the agent.
 * "If you get pinged on your ports, the whole system is aware."
 */

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RUNTIME_MONITOR_EVENT_MAX  32
#define RUNTIME_MONITOR_IP_MAX     64
#define RUNTIME_MONITOR_DETAIL_MAX 256
#define RUNTIME_MONITOR_AGENT_BUF  4096

typedef enum {
    RUNTIME_EVENT_SECURITY_REJECT,
    RUNTIME_EVENT_RATE_LIMIT,
    RUNTIME_EVENT_INJECTION,
    RUNTIME_EVENT_PATH_TRAVERSAL,
    RUNTIME_EVENT_PORT_PROBE,   /* many connections from same IP in short time */
    RUNTIME_EVENT_ERROR,
    RUNTIME_EVENT_OTHER
} runtime_event_type_t;

typedef struct {
    runtime_event_type_t type;
    char source[64];       /* e.g. "gateway", "security_monitor" */
    char ip[RUNTIME_MONITOR_IP_MAX];
    char details[RUNTIME_MONITOR_DETAIL_MAX];
    uint64_t timestamp_sec;
} runtime_monitor_event_t;

/** Initialize runtime monitor. Call once at startup (e.g. from daemon/gateway). */
int runtime_monitor_init(void);

/** Shutdown and free state. */
void runtime_monitor_shutdown(void);

/** Enable/disable sending alerts to the user via channels (Telegram/Discord/Slack). */
void runtime_monitor_set_notify_user(bool enable);

/** Enable/disable injecting recent events into agent context so the model is aware. */
void runtime_monitor_set_agent_aware(bool enable);

/**
 * Record an event. If notify_user is enabled, the user is notified via channels.
 * Event is always appended to recent ring for agent context if agent_aware.
 */
void runtime_monitor_event(
    runtime_event_type_t type,
    const char *source,
    const char *ip,
    const char *details
);

/**
 * Call from gateway on each new connection (optional). Used to detect port probes:
 * many connections from same IP in a short window triggers RUNTIME_EVENT_PORT_PROBE.
 */
void runtime_monitor_connection(const char *ip);

/**
 * Get a summary of recent events for agent context. Writes into buf a single
 * string (e.g. "System alerts: [18:45] Rate limit from 1.2.3.4; [18:46] Injection attempt from 5.6.7.8.").
 * Returns 0 if something was written, -1 if no events or buf_size too small.
 */
int runtime_monitor_get_recent_for_agent(char *buf, size_t buf_size);

/** Get last N events (for APIs or debugging). */
int runtime_monitor_get_recent_events(runtime_monitor_event_t *out, size_t max_count, size_t *count);

#endif
