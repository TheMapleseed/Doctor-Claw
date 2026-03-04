#ifndef DOCTORCLAW_OBSERVABILITY_H
#define DOCTORCLAW_OBSERVABILITY_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_METRICS 128
#define MAX_METRIC_NAME 128
#define MAX_LABELS 16

typedef struct {
    char metric_name[MAX_METRIC_NAME];
    double value;
    uint64_t timestamp;
    char labels[MAX_LABELS][128];
    size_t label_count;
} metric_t;

typedef struct {
    metric_t metrics[MAX_METRICS];
    size_t count;
    char prometheus_addr[256];
    char otlp_endpoint[256];
    bool prometheus_enabled;
    bool otlp_enabled;
} observability_t;

typedef enum {
    METRIC_COUNTER,
    METRIC_GAUGE,
    METRIC_HISTOGRAM
} metric_type_t;

int observability_init(observability_t *obs);
int observability_record(observability_t *obs, const char *name, double value);
int observability_record_with_labels(observability_t *obs, const char *name, double value, const char *labels[]);
int observability_get(observability_t *obs, const char *name, double *out_value);
void observability_reset(observability_t *obs);

int observability_prometheus_init(observability_t *obs, const char *listen_addr);
int observability_prometheus_export(observability_t *obs, char *output, size_t output_size);
int observability_prometheus_scrape(observability_t *obs, char *output, size_t output_size);

int observability_otlp_init(observability_t *obs, const char *endpoint);
int observability_otlp_export(observability_t *obs);
int observability_otlp_shutdown(observability_t *obs);

int observability_log(const char *level, const char *message);

#endif
