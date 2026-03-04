#ifndef DOCTORCLAW_HEARTBEAT_H
#define DOCTORCLAW_HEARTBEAT_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t last_beat;
    uint64_t interval_ms;
    bool active;
} heartbeat_t;

int heartbeat_init(heartbeat_t *hb, uint64_t interval_ms);
int heartbeat_ping(heartbeat_t *hb);
bool heartbeat_is_alive(heartbeat_t *hb);
uint64_t heartbeat_since_last(heartbeat_t *hb);
void heartbeat_stop(heartbeat_t *hb);

#endif
