#ifndef DOCTORCLAW_CONFIG_H
#define DOCTORCLAW_CONFIG_H

#include "c23_check.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_PATH_LEN 4096
#define MAX_PROVIDER_NAME_LEN 64
#define MAX_MODEL_NAME_LEN 128
#define MAX_URL_LEN 512
#define MAX_API_KEY_LEN 256

typedef struct {
    char workspace_dir[MAX_PATH_LEN];
    char config_path[MAX_PATH_LEN];
    char state_dir[MAX_PATH_LEN];
    char data_dir[MAX_PATH_LEN];
    char cache_dir[MAX_PATH_LEN];
} config_paths_t;

typedef struct {
    char provider[MAX_PROVIDER_NAME_LEN];
    char model[MAX_MODEL_NAME_LEN];
    double temperature;
    size_t max_tokens;
    char api_url[MAX_URL_LEN];
    char api_key[MAX_API_KEY_LEN];
    bool stream;
    char ollama_host[256];
    char llama_model_path[MAX_PATH_LEN];
} provider_config_t;

typedef struct {
    char backend[32];
    bool auto_save;
    char storage_provider[32];
    size_t min_relevance_score;
    char embedding_model[64];
    size_t chunk_size;
    bool snapshot_enabled;
} memory_config_t;

typedef struct {
    bool enabled;
    unsigned int interval_minutes;
    char endpoint[256];
    char auth_token[128];
} heartbeat_config_t;

typedef struct {
    char backend[32];
    char otel_endpoint[256];
    char otel_service_name[128];
    bool verbose;
} observability_config_t;

typedef enum {
    AUTONOMY_LEVEL_FULL,
    AUTONOMY_LEVEL_SUPERVISED,
    AUTONOMY_LEVEL_RESTRICTED,
    AUTONOMY_LEVEL_NONE
} autonomy_level_t;

typedef struct {
    bool enabled;
    autonomy_level_t level;
    char level_str[16];
    bool workspace_only;
    char *allowed_commands[128];
    size_t allowed_command_count;
    unsigned int max_actions_per_hour;
    unsigned int max_cost_per_day_cents;
    bool require_approval;
    char approval_channel[64];
    bool sandbox_enabled;
    char sandbox_backend[32];
} autonomy_config_t;

typedef struct {
    bool enabled;
    unsigned short port;
    char host[256];
    unsigned int timeout_seconds;
    size_t max_body_size;
    bool rate_limit_enabled;
    unsigned int rate_limit_requests;
    unsigned int rate_limit_window_seconds;
    bool cors_enabled;
    char cors_origins[512];
} gateway_config_t;

typedef struct {
    bool enabled;
    bool encrypt;
    char provider[32];
    char encryption_key[256];
} secrets_config_t;

typedef struct {
    bool enabled;
    char provider[32];
    double input_cost_per_1m;
    double output_cost_per_1m;
} cost_config_t;

typedef struct {
    bool enabled;
    char browser_type[32];
    bool headless;
    char browser_path[256];
    unsigned int viewport_width;
    unsigned int viewport_height;
    bool computer_use_enabled;
    double max_duration_seconds;
} browser_config_t;

typedef struct {
    bool enabled;
    char provider[32];
    double timeout_seconds;
    bool follow_redirects;
    unsigned int max_response_size;
    char proxy_url[256];
} http_request_config_t;

typedef struct {
    bool enabled;
    char provider[32];
    char api_key[256];
    unsigned int max_results;
} web_search_config_t;

typedef struct {
    bool enabled;
    char api_key[256];
    char entity_id[128];
} composio_config_t;

typedef struct {
    bool enabled;
    char provider[32];
    char base_url[MAX_URL_LEN];
    unsigned int max_retries;
    unsigned int retry_delay_ms;
    bool fallback_enabled;
} reliability_config_t;

typedef struct {
    bool enabled;
    unsigned int max_history_messages;
    unsigned int max_tool_iterations;
    bool parallel_tools;
    char tool_dispatcher[32];
    unsigned int compact_threshold;
    bool auto_save;
} agent_config_t;

typedef struct {
    bool enabled;
    char storage_backend[32];
    char db_path[MAX_PATH_LEN];
    bool wal_mode;
    bool foreign_keys;
} storage_config_t;

typedef struct {
    char provider[MAX_PROVIDER_NAME_LEN];
    char model[MAX_MODEL_NAME_LEN];
    char hint[64];
    double temperature_override;
} model_route_t;

typedef struct {
    char pattern[256];
    char hint[64];
    double threshold;
} classification_rule_t;

typedef struct {
    classification_rule_t rules[32];
    size_t rule_count;
    bool enabled;
} query_classification_config_t;

typedef struct {
    bool enabled;
    char schedule[128];
    char command[512];
    char prompt[1024];
    bool allow_overlap;
} cron_job_t;

typedef struct {
    cron_job_t jobs[64];
    size_t job_count;
    bool enabled;
} cron_config_t;

typedef struct {
    provider_config_t provider;
    memory_config_t memory;
    heartbeat_config_t heartbeat;
    observability_config_t observability;
    autonomy_config_t autonomy;
    config_paths_t paths;
    gateway_config_t gateway;
    secrets_config_t secrets;
    cost_config_t cost;
    browser_config_t browser;
    http_request_config_t http_request;
    web_search_config_t web_search;
    composio_config_t composio;
    reliability_config_t reliability;
    agent_config_t agent;
    storage_config_t storage;
    cron_config_t cron;
    model_route_t model_routes[16];
    size_t model_route_count;
    query_classification_config_t query_classification;
    char default_provider[MAX_PROVIDER_NAME_LEN];
    char default_model[MAX_MODEL_NAME_LEN];
    double default_temperature;
    char api_key[MAX_API_KEY_LEN];
    char api_url[MAX_URL_LEN];
} config_t;

int config_load(const char *path, config_t *cfg);
int config_load_from_env(config_t *cfg);
int config_save(const config_t *cfg, const char *path);
void config_init_defaults(config_t *cfg);
void config_free(config_t *cfg);
const char* config_get_workspace_dir(const config_t *cfg);
int config_validate(const config_t *cfg, char *errors, size_t error_size);
int config_set_provider(config_t *cfg, const char *provider, const char *model);
int config_add_model_route(config_t *cfg, const char *provider, const char *model, const char *hint);
int config_add_cron_job(config_t *cfg, const char *schedule, const char *command);

/** Write a summary of detected env vars (which are set) to buf. Does not print secret values. */
int config_env_summary(char *buf, size_t buf_size);
/** Number of environment variable names the binary recognizes (canonical list). */
int config_env_var_count(void);
/** Name of the i-th env var (0 <= i < config_env_var_count()); NULL if out of range. */
const char *config_env_var_name(int i);

#endif
