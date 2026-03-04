#ifndef DOCTORCLAW_PROVIDERS_H
#define DOCTORCLAW_PROVIDERS_H

#include "c23_check.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_PROVIDER_NAME 64
#define MAX_MODEL_NAME 128
#define MAX_URL_LEN 512
#define MAX_PROVIDERS 16
#define MAX_TOOLS 32
#define MAX_TOOL_CALLS 16

typedef enum {
    PROVIDER_OPENROUTER,
    PROVIDER_ANTHROPIC,
    PROVIDER_OPENAI,
    PROVIDER_OPENAI_CODEX,
    PROVIDER_OLLAMA,
    PROVIDER_GEMINI,
    PROVIDER_GLM,
    PROVIDER_COPILOT,
    PROVIDER_LLAMA,
    PROVIDER_CUSTOM,
    PROVIDER_UNKNOWN
} provider_type_t;

typedef enum {
    ROUTING_STRATEGY_ROUND_ROBIN,
    ROUTING_STRATEGY_FAILOVER,
    ROUTING_STRATEGY_LOAD_BALANCE,
    ROUTING_STRATEGY_COST_OPTIMIZED
} routing_strategy_t;

typedef struct {
    char id[MAX_PROVIDER_NAME];
    char name[MAX_PROVIDER_NAME];
    char api_url[MAX_URL_LEN];
    char api_key[256];
    provider_type_t type;
    bool requires_auth;
    bool is_local;
    char **aliases;
    size_t alias_count;
} provider_info_t;

typedef struct {
    char model[MAX_MODEL_NAME];
    char provider[MAX_PROVIDER_NAME];
    size_t context_window;
    double input_cost_per_1m;
    double output_cost_per_1m;
} model_info_t;

typedef struct {
    char role[16];
    char content[8192];
} message_t;

typedef struct {
    char name[128];
    char arguments[4096];
} tool_call_t;

typedef struct {
    tool_call_t calls[MAX_TOOL_CALLS];
    size_t call_count;
} tool_calls_t;

typedef struct {
    message_t *messages;
    size_t message_count;
    size_t message_capacity;
    double temperature;
    size_t max_tokens;
    bool stream;
    tool_calls_t *tools;
} chat_context_t;

typedef struct {
    char content[16384];
    size_t tokens_used;
    size_t prompt_tokens;
    size_t completion_tokens;
    bool done;
    tool_calls_t tool_calls;
    char reasoning[4096];
} chat_response_t;

typedef struct {
    char *data;
    size_t size;
} http_response_t;

typedef struct {
    provider_type_t providers[MAX_PROVIDERS];
    size_t provider_count;
    routing_strategy_t strategy;
    int current_index;
    int retry_count;
    int timeout_ms;
} provider_router_t;

typedef struct {
    provider_type_t primary;
    provider_type_t fallback;
    int max_retries;
    int retry_delay_ms;
    bool auto_failover_enabled;
} reliable_config_t;

int providers_init(void);
void providers_shutdown(void);
int http_post(const char *url, const char *headers[], size_t header_count,
              const char *body, http_response_t *response);
int http_get(const char *url, const char *headers[], size_t header_count,
             http_response_t *response);
void http_response_free(http_response_t *response);

provider_type_t provider_get_type(const char *name);
const char* provider_get_name(provider_type_t type);
int provider_list_available(provider_info_t **out_providers, size_t *out_count);
int provider_list_models(provider_type_t type, model_info_t **out_models, size_t *out_count);
int provider_get_api_key(provider_type_t type, char *out_key, size_t out_len);

int provider_chat_completion(
    provider_type_t type,
    const char *model,
    const char *api_key,
    const chat_context_t *context,
    chat_response_t *response
);

int provider_chat_with_tools(
    provider_type_t type,
    const char *model,
    const char *api_key,
    const chat_context_t *context,
    const char *tools_json,
    chat_response_t *response
);

int provider_router_init(provider_router_t *router, const provider_type_t *providers, size_t count, routing_strategy_t strategy);
int provider_router_next(provider_router_t *router, provider_type_t *out_type);
int provider_router_select(provider_router_t *router, const char *model, provider_type_t *out_type);
void provider_router_free(provider_router_t *router);

int provider_reliable_init(reliable_config_t *config, provider_type_t primary, provider_type_t fallback);
int provider_reliable_call(reliable_config_t *config, const char *model, const chat_context_t *context, chat_response_t *response);
void provider_reliable_free(reliable_config_t *config);

int provider_openai_compatible_call(const char *base_url, const char *api_key, const char *model, const chat_context_t *context, chat_response_t *response);
int provider_anthropic_call(const char *api_key, const char *model, const chat_context_t *context, chat_response_t *response);
int provider_gemini_call(const char *api_key, const char *model, const chat_context_t *context, chat_response_t *response);

int provider_openai_with_tools(const char *api_key, const char *model, const chat_context_t *context, const char *tools_json, chat_response_t *response);
int provider_anthropic_with_tools(const char *api_key, const char *model, const chat_context_t *context, const char *tools_json, chat_response_t *response);

void chat_context_init(chat_context_t *ctx);
void chat_context_add_message(chat_context_t *ctx, const char *role, const char *content);
void chat_context_add_system_message(chat_context_t *ctx, const char *content);
void chat_context_clear_messages(chat_context_t *ctx);
void chat_context_free(chat_context_t *ctx);

extern bool curl_initialized;

int find_json_value(const char *json, const char *key, char *out_value, size_t out_len);

#endif
