#include "heartbeat.h"
#include <time.h>
#include <unistd.h>

int heartbeat_init(heartbeat_t *hb, uint64_t interval_ms) {
    if (!hb) return -1;
    hb->last_beat = (uint64_t)time(NULL) * 1000;
    hb->interval_ms = interval_ms;
    hb->active = true;
    return 0;
}

int heartbeat_ping(heartbeat_t *hb) {
    if (!hb || !hb->active) return -1;
    hb->last_beat = (uint64_t)time(NULL) * 1000;
    return 0;
}

bool heartbeat_is_alive(heartbeat_t *hb) {
    if (!hb || !hb->active) return false;
    uint64_t now = (uint64_t)time(NULL) * 1000;
    return (now - hb->last_beat) <= hb->interval_ms * 2;
}

uint64_t heartbeat_since_last(heartbeat_t *hb) {
    if (!hb) return 0;
    uint64_t now = (uint64_t)time(NULL) * 1000;
    return now - hb->last_beat;
}

void heartbeat_stop(heartbeat_t *hb) {
    if (hb) {
        hb->active = false;
    }
}
