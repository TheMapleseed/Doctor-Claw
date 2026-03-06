#include "observability.h"
#include "providers.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

static pthread_mutex_t g_obs_mutex = PTHREAD_MUTEX_INITIALIZER;

int observability_init(observability_t *obs) {
    if (!obs) return -1;
    memset(obs, 0, sizeof(observability_t));
    pthread_mutex_init(&g_obs_mutex, NULL);
    return 0;
}

int observability_record(observability_t *obs, const char *name, double value) {
    if (!obs || !name) return -1;
    
    pthread_mutex_lock(&g_obs_mutex);
    
    if (obs->count >= MAX_METRICS) {
        pthread_mutex_unlock(&g_obs_mutex);
        return -1;
    }
    
    metric_t *m = &obs->metrics[obs->count];
    snprintf(m->metric_name, sizeof(m->metric_name), "%s", name);
    m->value = value;
    m->timestamp = (uint64_t)time(NULL);
    m->label_count = 0;
    obs->count++;
    
    pthread_mutex_unlock(&g_obs_mutex);
    return 0;
}

int observability_record_with_labels(observability_t *obs, const char *name, double value, const char *labels[]) {
    if (!obs || !name) return -1;
    
    pthread_mutex_lock(&g_obs_mutex);
    
    if (obs->count >= MAX_METRICS) {
        pthread_mutex_unlock(&g_obs_mutex);
        return -1;
    }
    
    metric_t *m = &obs->metrics[obs->count];
    snprintf(m->metric_name, sizeof(m->metric_name), "%s", name);
    m->value = value;
    m->timestamp = (uint64_t)time(NULL);
    m->label_count = 0;
    
    if (labels) {
        for (size_t i = 0; labels[i] && m->label_count < MAX_LABELS; i++) {
            snprintf(m->labels[m->label_count], sizeof(m->labels[0]), "%s", labels[i]);
            m->label_count++;
        }
    }
    
    obs->count++;
    
    pthread_mutex_unlock(&g_obs_mutex);
    return 0;
}

int observability_get(observability_t *obs, const char *name, double *out_value) {
    if (!obs || !name || !out_value) return -1;
    
    pthread_mutex_lock(&g_obs_mutex);
    
    for (size_t i = 0; i < obs->count; i++) {
        if (strcmp(obs->metrics[i].metric_name, name) == 0) {
            *out_value = obs->metrics[i].value;
            pthread_mutex_unlock(&g_obs_mutex);
            return 0;
        }
    }
    
    pthread_mutex_unlock(&g_obs_mutex);
    return -1;
}

void observability_reset(observability_t *obs) {
    if (!obs) return;
    pthread_mutex_lock(&g_obs_mutex);
    memset(obs->metrics, 0, sizeof(obs->metrics));
    obs->count = 0;
    pthread_mutex_unlock(&g_obs_mutex);
}

int observability_prometheus_init(observability_t *obs, const char *listen_addr) {
    if (!obs || !listen_addr) return -1;
    
    snprintf(obs->prometheus_addr, sizeof(obs->prometheus_addr), "%s", listen_addr);
    obs->prometheus_enabled = true;
    
    printf("[Observability] Prometheus exporter initialized at %s\n", listen_addr);
    return 0;
}

int observability_prometheus_export(observability_t *obs, char *output, size_t output_size) {
    if (!obs || !output) return -1;
    
    pthread_mutex_lock(&g_obs_mutex);
    
    size_t offset = 0;
    (void)time(NULL);
    
    offset += snprintf(output + offset, output_size - offset, "# HELP doctorclaw_metric A metric\n");
    offset += snprintf(output + offset, output_size - offset, "# TYPE doctorclaw_metric gauge\n");
    
    for (size_t i = 0; i < obs->count && offset < output_size - 100; i++) {
        metric_t *m = &obs->metrics[i];
        
        if (m->label_count > 0) {
            offset += snprintf(output + offset, output_size - offset, 
                "doctorclaw_%s{%s} %f %llu\n",
                m->metric_name, m->labels[0], m->value, (unsigned long long)m->timestamp);
        } else {
            offset += snprintf(output + offset, output_size - offset, 
                "doctorclaw_%s %f %llu\n",
                m->metric_name, m->value, (unsigned long long)m->timestamp);
        }
    }
    
    pthread_mutex_unlock(&g_obs_mutex);
    
    return 0;
}

int observability_prometheus_scrape(observability_t *obs, char *output, size_t output_size) {
    return observability_prometheus_export(obs, output, output_size);
}

int observability_otlp_init(observability_t *obs, const char *endpoint) {
    if (!obs || !endpoint) return -1;
    
    snprintf(obs->otlp_endpoint, sizeof(obs->otlp_endpoint), "%s", endpoint);
    obs->otlp_enabled = true;
    
    printf("[Observability] OTLP exporter initialized to %s\n", endpoint);
    return 0;
}

int observability_otlp_export(observability_t *obs) {
    if (!obs || !obs->otlp_enabled) return -1;
    
    pthread_mutex_lock(&g_obs_mutex);
    
    char json_payload[16384] = {0};
    size_t offset = 0;
    
    offset += snprintf(json_payload + offset, sizeof(json_payload) - offset, "{\"resourceMetrics\":[{\"scopeMetrics\":[{\"metrics\":[");
    
    for (size_t i = 0; i < obs->count && offset < sizeof(json_payload) - 200; i++) {
        metric_t *m = &obs->metrics[i];
        
        if (i > 0) offset += snprintf(json_payload + offset, sizeof(json_payload) - offset, ",");
        
        offset += snprintf(json_payload + offset, sizeof(json_payload) - offset,
            "{\"name\":\"%s\",\"gauge\":{\"dataPoints\":[{\"asDouble\":%f,\"timeUnixNano\":%llu}]}}",
            m->metric_name, m->value, (unsigned long long)m->timestamp * 1000000000ULL);
    }
    
    offset += snprintf(json_payload + offset, sizeof(json_payload) - offset, "]}]}]}");
    
    char url[512];
    snprintf(url, sizeof(url), "%s/v1/metrics", obs->otlp_endpoint);
    
    const char *headers[] = {"Content-Type: application/json"};
    http_response_t resp = {0};
    int result = http_post(url, headers, 1, json_payload, &resp);
    
    pthread_mutex_unlock(&g_obs_mutex);
    
    http_response_free(&resp);
    return result;
}

int observability_otlp_shutdown(observability_t *obs) {
    if (!obs) return -1;
    obs->otlp_enabled = false;
    printf("[Observability] OTLP exporter shut down\n");
    return 0;
}

int observability_log(const char *level, const char *message) {
    if (!level || !message) return -1;
    
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", tm_info);
    
    if (strcmp(level, "error") == 0 || strcmp(level, "err") == 0) {
        fprintf(stderr, "[%s] ERROR: %s\n", time_str, message);
    } else if (strcmp(level, "warn") == 0 || strcmp(level, "warning") == 0) {
        fprintf(stderr, "[%s] WARN: %s\n", time_str, message);
    } else if (strcmp(level, "debug") == 0 || strcmp(level, "dbg") == 0) {
        fprintf(stdout, "[%s] DEBUG: %s\n", time_str, message);
    } else {
        printf("[%s] INFO: %s\n", time_str, message);
    }
    
    return 0;
}
