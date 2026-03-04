#include "cost.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int cost_tracker_init(cost_tracker_t *tracker) {
    if (!tracker) return -1;
    memset(tracker, 0, sizeof(cost_tracker_t));
    return 0;
}

int cost_track(cost_tracker_t *tracker, const char *provider, const char *model, uint64_t prompt_tokens, uint64_t completion_tokens) {
    if (!tracker || !provider || !model) return -1;
    if (tracker->entry_count >= 256) return -1;
    
    cost_entry_t *entry = &tracker->entries[tracker->entry_count];
    snprintf(entry->provider, sizeof(entry->provider), "%s", provider);
    snprintf(entry->model, sizeof(entry->model), "%s", model);
    entry->prompt_tokens = prompt_tokens;
    entry->completion_tokens = completion_tokens;
    entry->timestamp = (uint64_t)time(NULL);
    
    double prompt_cost = (prompt_tokens / 1000.0) * entry->prompt_cost_per_1k;
    double completion_cost = (completion_tokens / 1000.0) * entry->completion_cost_per_1k;
    tracker->total_cost += prompt_cost + completion_cost;
    
    tracker->entry_count++;
    return 0;
}

int cost_get_total(cost_tracker_t *tracker, double *out_total) {
    if (!tracker || !out_total) return -1;
    *out_total = tracker->total_cost;
    return 0;
}

int cost_get_by_provider(cost_tracker_t *tracker, const char *provider, double *out_cost) {
    if (!tracker || !provider || !out_cost) return -1;
    *out_cost = 0.0;
    
    for (size_t i = 0; i < tracker->entry_count; i++) {
        if (strcmp(tracker->entries[i].provider, provider) == 0) {
            double prompt_cost = (tracker->entries[i].prompt_tokens / 1000.0) * tracker->entries[i].prompt_cost_per_1k;
            double completion_cost = (tracker->entries[i].completion_tokens / 1000.0) * tracker->entries[i].completion_cost_per_1k;
            *out_cost += prompt_cost + completion_cost;
        }
    }
    return 0;
}

int cost_export(cost_tracker_t *tracker, const char *path) {
    if (!tracker || !path) return -1;
    
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    
    fprintf(f, "Provider,Model,PromptTokens,CompletionTokens,Cost,Timestamp\n");
    for (size_t i = 0; i < tracker->entry_count; i++) {
        cost_entry_t *e = &tracker->entries[i];
        double cost = (e->prompt_tokens / 1000.0) * e->prompt_cost_per_1k + 
                      (e->completion_tokens / 1000.0) * e->completion_cost_per_1k;
        fprintf(f, "%s,%s,%" PRIu64 ",%" PRIu64 ",%.6f,%" PRIu64 "\n", e->provider, e->model,
                e->prompt_tokens, e->completion_tokens, cost, e->timestamp);
    }
    fprintf(f, "\nTotal Cost: $%.6f\n", tracker->total_cost);
    
    fclose(f);
    return 0;
}

void cost_tracker_free(cost_tracker_t *tracker) {
    if (tracker) {
        memset(tracker, 0, sizeof(cost_tracker_t));
    }
}
