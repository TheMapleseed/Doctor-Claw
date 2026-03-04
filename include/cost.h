#ifndef DOCTORCLAW_COST_H
#define DOCTORCLAW_COST_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char provider[64];
    char model[128];
    uint64_t prompt_tokens;
    uint64_t completion_tokens;
    double prompt_cost_per_1k;
    double completion_cost_per_1k;
    uint64_t timestamp;
} cost_entry_t;

typedef struct {
    cost_entry_t entries[256];
    size_t entry_count;
    double total_cost;
} cost_tracker_t;

int cost_tracker_init(cost_tracker_t *tracker);
int cost_track(cost_tracker_t *tracker, const char *provider, const char *model, uint64_t prompt_tokens, uint64_t completion_tokens);
int cost_get_total(cost_tracker_t *tracker, double *out_total);
int cost_get_by_provider(cost_tracker_t *tracker, const char *provider, double *out_cost);
int cost_export(cost_tracker_t *tracker, const char *path);
void cost_tracker_free(cost_tracker_t *tracker);

#endif
