#include "providers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>
#include <unistd.h>
#include <curl/curl.h>

#define MAX_RETRIES 3
#define RATE_LIMIT_DELAY_MS 100

typedef struct {
    time_t last_request_time;
    int requests_this_minute;
    int max_requests_per_minute;
} rate_limit_t;

static CURL *global_curl = NULL;
bool curl_initialized = false;
static rate_limit_t g_rate_limits[16] = {0};
static size_t g_rate_limit_count = 0;

static size_t write_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    http_response_t *resp = (http_response_t *)userp;
    
    char *ptr = realloc(resp->data, resp->size + realsize + 1);
    if (!ptr) {
        return 0;
    }
    
    resp->data = ptr;
    memcpy(&(resp->data[resp->size]), contents, realsize);
    resp->size += realsize;
    resp->data[resp->size] = 0;
    
    return realsize;
}

static __attribute__((unused)) void url_encode(const char *src, char *dest, size_t dest_len) {
    const char *hex = "0123456789ABCDEF";
    size_t j = 0;
    
    for (size_t i = 0; src[i] && j < dest_len - 1; i++) {
        if (isalnum((unsigned char)src[i]) || src[i] == '-' || src[i] == '_' || src[i] == '.' || src[i] == '~') {
            dest[j++] = src[i];
        } else {
            if (j + 3 < dest_len) {
                dest[j++] = '%';
                dest[j++] = hex[(src[i] >> 4) & 0xF];
                dest[j++] = hex[src[i] & 0xF];
            }
        }
    }
    dest[j] = '\0';
}

int find_json_value(const char *json, const char *key, char *out_value, size_t out_len) {
    if (!json || !key || !out_value) return -1;
    
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    
    const char *found = strstr(json, search);
    if (!found) return -1;
    
    found = strchr(found, ':');
    if (!found) return -1;
    
    found++;
    while (*found && isspace(*found)) found++;
    
    if (*found == '"') {
        found++;
        const char *end = strchr(found, '"');
        if (!end) return -1;
        size_t len = end - found;
        if (len >= out_len) len = out_len - 1;
        memcpy(out_value, found, len);
        out_value[len] = '\0';
    } else if (*found == '{' || *found == '[') {
        int depth = 0;
        const char *start = found;
        while (*found) {
            if (*found == '{' || *found == '[') depth++;
            else if (*found == '}' || *found == ']') depth--;
            if (depth == 0) break;
            found++;
        }
        size_t len = found - start + 1;
        if (len >= out_len) len = out_len - 1;
        memcpy(out_value, start, len);
        out_value[len] = '\0';
    } else {
        const char *end = found;
        while (*end && !isspace(*end) && *end != ',' && *end != '}') end++;
        size_t len = end - found;
        if (len >= out_len) len = out_len - 1;
        memcpy(out_value, found, len);
        out_value[len] = '\0';
    }
    
    return 0;
}

static int find_json_array_value(const char *json, const char *key, int index, char *out_value, size_t out_len) {
    if (!json || !key || !out_value) return -1;
    
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    
    const char *found = strstr(json, search);
    if (!found) return -1;
    
    found = strchr(found, ':');
    if (!found) return -1;
    
    found++;
    while (*found && (*found != '[')) found++;
    if (*found != '[') return -1;
    
    found++;
    int current = 0;
    int depth = 1;
    
    while (*found && depth > 0) {
        if (*found == '[') depth++;
        else if (*found == ']') depth--;
        else if (depth == 1 && *found == '"') {
            const char *end = ++found;
            while (*end && *end != '"') end++;
            if (current == index) {
                size_t len = end - found;
                if (len >= out_len) len = out_len - 1;
                memcpy(out_value, found, len);
                out_value[len] = '\0';
                return 0;
            }
            found = end;
            current++;
        }
        found++;
    }
    
    return -1;
}

static void check_rate_limit(provider_type_t type) {
    (void)type;
    time_t now = time(NULL);
    
    for (size_t i = 0; i < g_rate_limit_count; i++) {
        if (g_rate_limits[i].requests_this_minute > 0) {
            if (now - g_rate_limits[i].last_request_time > 60) {
                g_rate_limits[i].requests_this_minute = 0;
            }
        }
        
        if (g_rate_limits[i].requests_this_minute >= g_rate_limits[i].max_requests_per_minute) {
            sleep(1);
        }
    }
}

static void update_rate_limit(provider_type_t type) {
    (void)type;
    time_t now = time(NULL);
    
    for (size_t i = 0; i < g_rate_limit_count; i++) {
        if (now - g_rate_limits[i].last_request_time > 60) {
            g_rate_limits[i].requests_this_minute = 0;
        }
        g_rate_limits[i].requests_this_minute++;
        g_rate_limits[i].last_request_time = now;
    }
}

int providers_init(void) {
    if (curl_initialized) return 0;
    
    global_curl = curl_easy_init();
    if (!global_curl) return -1;
    
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_initialized = true;
    
    g_rate_limits[0].max_requests_per_minute = 60;
    g_rate_limit_count = 1;
    
    return 0;
}

void providers_shutdown(void) {
    if (global_curl) {
        curl_easy_cleanup(global_curl);
        global_curl = NULL;
    }
    curl_initialized = false;
}

int http_post(const char *url, const char *headers[], size_t header_count,
              const char *body, http_response_t *response) {
    if (!curl_initialized) {
        providers_init();
    }
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    response->data = malloc(1);
    response->size = 0;
    
    struct curl_slist *header_list = NULL;
    for (size_t i = 0; i < header_count; i++) {
        header_list = curl_slist_append(header_list, headers[i]);
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }
    
    CURLcode res = curl_easy_perform(curl);
    
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    if (header_list) {
        curl_slist_free_all(header_list);
    }
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK && http_code >= 200 && http_code < 300) ? 0 : -1;
}

int http_get(const char *url, const char *headers[], size_t header_count,
             http_response_t *response) {
    if (!curl_initialized) {
        providers_init();
    }
    
    CURL *curl = curl_easy_init();
    if (!curl) return -1;
    
    response->data = malloc(1);
    response->size = 0;
    
    struct curl_slist *header_list = NULL;
    for (size_t i = 0; i < header_count; i++) {
        header_list = curl_slist_append(header_list, headers[i]);
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }
    
    CURLcode res = curl_easy_perform(curl);
    
    if (header_list) {
        curl_slist_free_all(header_list);
    }
    curl_easy_cleanup(curl);
    
    return (res == CURLE_OK) ? 0 : -1;
}

void http_response_free(http_response_t *response) {
    if (response->data) {
        free(response->data);
        response->data = NULL;
    }
    response->size = 0;
}

provider_type_t provider_get_type(const char *name) {
    if (strcmp(name, "openrouter") == 0) return PROVIDER_OPENROUTER;
    if (strcmp(name, "anthropic") == 0) return PROVIDER_ANTHROPIC;
    if (strcmp(name, "openai") == 0) return PROVIDER_OPENAI;
    if (strcmp(name, "openai-codex") == 0) return PROVIDER_OPENAI_CODEX;
    if (strcmp(name, "ollama") == 0) return PROVIDER_OLLAMA;
    if (strcmp(name, "gemini") == 0) return PROVIDER_GEMINI;
    if (strcmp(name, "glm") == 0) return PROVIDER_GLM;
    if (strcmp(name, "copilot") == 0) return PROVIDER_COPILOT;
    if (strcmp(name, "llama") == 0) return PROVIDER_LLAMA;
    if (strncmp(name, "custom:", 7) == 0) return PROVIDER_CUSTOM;
    return PROVIDER_UNKNOWN;
}

const char* provider_get_name(provider_type_t type) {
    switch (type) {
        case PROVIDER_OPENROUTER: return "openrouter";
        case PROVIDER_ANTHROPIC: return "anthropic";
        case PROVIDER_OPENAI: return "openai";
        case PROVIDER_OPENAI_CODEX: return "openai-codex";
        case PROVIDER_OLLAMA: return "ollama";
        case PROVIDER_GEMINI: return "gemini";
        case PROVIDER_GLM: return "glm";
        case PROVIDER_COPILOT: return "copilot";
        case PROVIDER_LLAMA: return "llama";
        case PROVIDER_CUSTOM: return "custom";
        default: return "unknown";
    }
}

void chat_context_init(chat_context_t *ctx) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(chat_context_t));
    ctx->message_capacity = 32;
    ctx->messages = calloc(ctx->message_capacity, sizeof(message_t));
    ctx->temperature = 0.7;
    ctx->max_tokens = 4096;
    ctx->stream = false;
}

void chat_context_add_message(chat_context_t *ctx, const char *role, const char *content) {
    if (!ctx || !ctx->messages || !role || !content) return;
    
    if (ctx->message_count >= ctx->message_capacity) {
        ctx->message_capacity *= 2;
        ctx->messages = realloc(ctx->messages, ctx->message_capacity * sizeof(message_t));
    }
    
    message_t *msg = &ctx->messages[ctx->message_count++];
    snprintf(msg->role, sizeof(msg->role), "%s", role);
    snprintf(msg->content, sizeof(msg->content), "%s", content);
}

void chat_context_add_system_message(chat_context_t *ctx, const char *content) {
    if (!ctx || !content) return;
    
    if (ctx->message_count > 0 && strcmp(ctx->messages[0].role, "system") == 0) {
        snprintf(ctx->messages[0].content, sizeof(ctx->messages[0].content), "%s", content);
    } else {
        memmove(&ctx->messages[1], ctx->messages, ctx->message_count * sizeof(message_t));
        snprintf(ctx->messages[0].role, sizeof(ctx->messages[0].role), "system");
        snprintf(ctx->messages[0].content, sizeof(ctx->messages[0].content), "%s", content);
        ctx->message_count++;
    }
}

void chat_context_clear_messages(chat_context_t *ctx) {
    if (!ctx || !ctx->messages) return;
    ctx->message_count = 0;
}

void chat_context_free(chat_context_t *ctx) {
    if (!ctx) return;
    if (ctx->messages) {
        free(ctx->messages);
        ctx->messages = NULL;
    }
    ctx->message_count = 0;
    ctx->message_capacity = 0;
}

static int build_messages_json(const chat_context_t *context, char *out_json, size_t out_len) {
    if (!context || !out_json) return -1;
    
    char *p = out_json;
    size_t remaining = out_len;
    int first = 1;
    
    for (size_t i = 0; i < context->message_count && remaining > 0; i++) {
        if (!first) {
            int written = snprintf(p, remaining, ",");
            p += written;
            remaining -= written;
        }
        first = 0;
        
        int written = snprintf(p, remaining, "{\"role\":\"%s\",\"content\":\"",
                              context->messages[i].role);
        p += written;
        remaining -= written;
        
        const char *content = context->messages[i].content;
        for (size_t j = 0; content[j] && remaining > 2; j++) {
            if (content[j] == '"') {
                if (remaining >= 2) {
                    *p++ = '\\';
                    *p++ = '"';
                    remaining -= 2;
                }
            } else if (content[j] == '\\') {
                if (remaining >= 2) {
                    *p++ = '\\';
                    *p++ = '\\';
                    remaining -= 2;
                }
            } else if (content[j] == '\n') {
                if (remaining >= 2) {
                    *p++ = '\\';
                    *p++ = 'n';
                    remaining -= 2;
                }
            } else {
                *p++ = content[j];
                remaining--;
            }
        }
        
        written = snprintf(p, remaining, "\"}");
        p += written;
        remaining -= written;
    }
    
    return 0;
}

static int parse_response_content(const char *json_response, char *out_content, size_t out_len) {
    if (!json_response || !out_content) return -1;
    
    if (find_json_value(json_response, "content", out_content, out_len) == 0) {
        return 0;
    }
    
    if (find_json_array_value(json_response, "choices", 0, out_content, out_len) == 0) {
        char message_content[8192] = {0};
        if (find_json_value(out_content, "message", message_content, sizeof(message_content)) == 0) {
            char actual_content[8192] = {0};
            if (find_json_value(message_content, "content", actual_content, sizeof(actual_content)) == 0) {
                snprintf(out_content, out_len, "%s", actual_content);
                return 0;
            }
        }
    }
    
    char error[512] = {0};
    if (find_json_value(json_response, "error", error, sizeof(error)) == 0) {
        snprintf(out_content, out_len, "Error: %s", error);
        return 0;
    }
    
    return -1;
}

static int provider_openai_completion(
    const char *api_key,
    const char *model,
    const chat_context_t *context,
    chat_response_t *response,
    const char *api_url
) {
    char url[512];
    char auth_header[512];
    char body[16384];
    char messages_json[8192];
    
    snprintf(url, sizeof(url), "%s/chat/completions", api_url);
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    
    build_messages_json(context, messages_json, sizeof(messages_json));
    
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"messages\":[%s],\"temperature\":%f,\"max_tokens\":%zu,\"stream\":false}",
             model, messages_json, context->temperature, context->max_tokens);
    
    const char *headers[] = {auth_header, "Content-Type: application/json", "Accept: application/json"};
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 3, body, &resp);
    
    if (result == 0 && resp.data) {
        parse_response_content(resp.data, response->content, sizeof(response->content));
        response->done = true;
        
        char usage[64];
        if (find_json_value(resp.data, "usage", usage, sizeof(usage)) == 0) {
            char prompt_tokens[32], completion_tokens[32], total_tokens[32];
            if (find_json_value(usage, "prompt_tokens", prompt_tokens, sizeof(prompt_tokens)) == 0) {
                response->prompt_tokens = atoi(prompt_tokens);
            }
            if (find_json_value(usage, "completion_tokens", completion_tokens, sizeof(completion_tokens)) == 0) {
                response->completion_tokens = atoi(completion_tokens);
            }
            if (find_json_value(usage, "total_tokens", total_tokens, sizeof(total_tokens)) == 0) {
                response->tokens_used = atoi(total_tokens);
            }
        }
    } else {
        snprintf(response->content, sizeof(response->content), "HTTP request failed");
    }
    
    http_response_free(&resp);
    return result;
}

static int provider_anthropic_completion(
    const char *api_key,
    const char *model,
    const chat_context_t *context,
    chat_response_t *response
) {
    char url[] = "https://api.anthropic.com/v1/messages";
    char auth_header[512];
    char version_header[] = "anthropic-version: 2023-06-01";
    char body[16384];
    char messages_json[8192];
    
    snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
    
    build_messages_json(context, messages_json, sizeof(messages_json));
    
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"messages\":[%s],\"temperature\":%f,\"max_tokens\":%zu}",
             model, messages_json, context->temperature, context->max_tokens);
    
    const char *headers[] = {auth_header, version_header, "Content-Type: application/json"};
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 3, body, &resp);
    
    if (result == 0 && resp.data) {
        find_json_value(resp.data, "content", response->content, sizeof(response->content));
        response->done = true;
        
        char usage[64];
        if (find_json_value(resp.data, "usage", usage, sizeof(usage)) == 0) {
            char input_tokens[32], output_tokens[32];
            if (find_json_value(usage, "input_tokens", input_tokens, sizeof(input_tokens)) == 0) {
                response->prompt_tokens = atoi(input_tokens);
            }
            if (find_json_value(usage, "output_tokens", output_tokens, sizeof(output_tokens)) == 0) {
                response->completion_tokens = atoi(output_tokens);
                response->tokens_used = response->prompt_tokens + response->completion_tokens;
            }
        }
    } else {
        snprintf(response->content, sizeof(response->content), "HTTP request failed");
    }
    
    http_response_free(&resp);
    return result;
}

static int provider_gemini_completion(
    const char *api_key,
    const char *model,
    const chat_context_t *context,
    chat_response_t *response
) {
    char url[512];
    char auth_header[512];
    char body[16384];
    
    snprintf(url, sizeof(url), 
             "https://generativelanguage.googleapis.com/v1/models/%s:generateContent?key=%s",
             model, api_key);
    snprintf(auth_header, sizeof(auth_header), "Content-Type: application/json");
    
    char contents[8192] = {0};
    char *p = contents;
    size_t remaining = sizeof(contents);
    
    for (size_t i = 0; i < context->message_count && remaining > 0; i++) {
        int written = snprintf(p, remaining, "{\"role\":\"%s\",\"parts\":[{\"text\":\"%s\"}]}",
                              context->messages[i].role,
                              context->messages[i].content);
        p += written;
        remaining -= written;
        
        if (i < context->message_count - 1 && remaining > 0) {
            strncat(contents, ",", remaining - 1);
            p++;
            remaining--;
        }
    }
    
    snprintf(body, sizeof(body),
             "{\"contents\":[%s],\"generationConfig\":{\"temperature\":%f,\"maxOutputTokens\":%zu}}",
             contents, context->temperature, context->max_tokens);
    
    const char *headers[] = {auth_header};
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 1, body, &resp);
    
    if (result == 0 && resp.data) {
        char candidate[16384];
        if (find_json_array_value(resp.data, "candidates", 0, candidate, sizeof(candidate)) == 0) {
            find_json_value(candidate, "text", response->content, sizeof(response->content));
        }
        response->done = true;
    } else {
        snprintf(response->content, sizeof(response->content), "HTTP request failed");
    }
    
    http_response_free(&resp);
    return result;
}

static int provider_glm_completion(
    const char *api_key,
    const char *model,
    const chat_context_t *context,
    chat_response_t *response
) {
    char url[512];
    char auth_header[512];
    char body[16384];
    char messages_json[8192];
    
    snprintf(url, sizeof(url), "https://open.bigmodel.cn/api/paas/v4/chat/completions");
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    
    build_messages_json(context, messages_json, sizeof(messages_json));
    
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"messages\":[%s],\"temperature\":%f,\"max_tokens\":%zu}",
             model, messages_json, context->temperature, context->max_tokens);
    
    const char *headers[] = {auth_header, "Content-Type: application/json"};
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 2, body, &resp);
    
    if (result == 0 && resp.data) {
        parse_response_content(resp.data, response->content, sizeof(response->content));
        response->done = true;
    } else {
        snprintf(response->content, sizeof(response->content), "HTTP request failed");
    }
    
    http_response_free(&resp);
    return result;
}

static int provider_ollama_completion(
    const char *model,
    const chat_context_t *context,
    chat_response_t *response
) {
    char url[512];
    char body[16384];
    char messages_json[8192];
    
    snprintf(url, sizeof(url), "http://localhost:11434/api/chat");
    
    build_messages_json(context, messages_json, sizeof(messages_json));
    
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"messages\":[%s],\"temperature\":%f,\"stream\":false,\"options\":{\"num_predict\":%zu}}",
             model, messages_json, context->temperature, context->max_tokens);
    
    const char *headers[] = {"Content-Type: application/json"};
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 1, body, &resp);
    
    if (result == 0 && resp.data) {
        find_json_value(resp.data, "content", response->content, sizeof(response->content));
        response->done = true;
        response->tokens_used = strlen(response->content) / 4;
    } else {
        snprintf(response->content, sizeof(response->content), 
                 "Error: Could not connect to Ollama. Make sure Ollama is running (ollama serve)");
    }
    
    http_response_free(&resp);
    return result;
}

static int provider_copilot_completion(
    const char *api_key,
    const char *model,
    const chat_context_t *context,
    chat_response_t *response
) {
    char url[] = "https://api.github.com/copilot-models/chat/completions";
    char auth_header[512];
    char body[16384];
    char messages_json[8192];
    
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    
    build_messages_json(context, messages_json, sizeof(messages_json));
    
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"messages\":[%s],\"temperature\":%f,\"max_tokens\":%zu}",
             model[0] ? model : "gpt-4", messages_json, context->temperature, context->max_tokens);
    
    const char *headers[] = {auth_header, "Content-Type: application/json", "Accept: application/json"};
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 3, body, &resp);
    
    if (result == 0 && resp.data) {
        parse_response_content(resp.data, response->content, sizeof(response->content));
        response->done = true;
    } else {
        snprintf(response->content, sizeof(response->content), 
                 "Error: GitHub Copilot not available. Set GITHUB_TOKEN environment variable.");
    }
    
    http_response_free(&resp);
    return result;
}

int provider_chat_completion(
    provider_type_t type,
    const char *model,
    const char *api_key,
    const chat_context_t *context,
    chat_response_t *response
) {
    if (!response || !context) return -1;
    
    memset(response, 0, sizeof(chat_response_t));
    
    if (!api_key) {
        api_key = getenv("OPENAI_API_KEY");
        if (!api_key) {
            api_key = getenv("ANTHROPIC_API_KEY");
            if (!api_key) {
                api_key = getenv("OPENROUTER_API_KEY");
                if (!api_key) {
                    api_key = getenv("GEMINI_API_KEY");
                }
            }
        }
    }
    
    if (!api_key && type != PROVIDER_OLLAMA) {
        snprintf(response->content, sizeof(response->content),
                 "Error: No API key configured. Set OPENAI_API_KEY, ANTHROPIC_API_KEY, OPENROUTER_API_KEY, or GEMINI_API_KEY");
        response->done = true;
        return -1;
    }
    
    check_rate_limit(type);
    
    int result = 0;
    
    switch (type) {
        case PROVIDER_OPENAI:
        case PROVIDER_OPENAI_CODEX:
            result = provider_openai_completion(
                api_key,
                model[0] ? model : "gpt-4o",
                context, response,
                "https://api.openai.com/v1"
            );
            break;
            
        case PROVIDER_ANTHROPIC:
            result = provider_anthropic_completion(
                api_key,
                model[0] ? model : "claude-3-5-sonnet-20241022",
                context, response
            );
            break;
            
        case PROVIDER_OPENROUTER:
            result = provider_openai_completion(
                api_key,
                model[0] ? model : "openai/gpt-4o-mini",
                context, response,
                "https://openrouter.ai/api/v1"
            );
            break;
            
        case PROVIDER_GEMINI:
            result = provider_gemini_completion(
                api_key,
                model[0] ? model : "gemini-pro",
                context, response
            );
            break;
            
        case PROVIDER_GLM:
            result = provider_glm_completion(
                api_key,
                model[0] ? model : "glm-4",
                context, response
            );
            break;
            
        case PROVIDER_OLLAMA:
            result = provider_ollama_completion(
                model[0] ? model : "llama3.2",
                context, response
            );
            break;
            
        case PROVIDER_COPILOT: {
            const char *github_token = getenv("GITHUB_TOKEN");
            if (!github_token) {
                github_token = api_key;
            }
            result = provider_copilot_completion(
                github_token,
                model,
                context, response
            );
            break;
        }
            
        case PROVIDER_LLAMA:
            snprintf(response->content, sizeof(response->content),
                     "Error: llama.cpp requires loading a GGUF model. Use the llama CLI tool directly.");
            response->done = true;
            return -1;
            
        case PROVIDER_CUSTOM:
        case PROVIDER_UNKNOWN:
        default:
            snprintf(response->content, sizeof(response->content), 
                     "Error: Unknown or unsupported provider");
            response->done = true;
            return -1;
    }
    
    update_rate_limit(type);
    
    return result;
}

int provider_list_models(provider_type_t type, model_info_t **out_models, size_t *out_count) {
    static model_info_t openai_models[] = {
        {"gpt-4o", "openai", 128000, 2.50, 10.00},
        {"gpt-4o-mini", "openai", 128000, 0.15, 0.60},
        {"gpt-4-turbo", "openai", 128000, 10.00, 30.00},
        {"gpt-4", "openai", 8192, 30.00, 60.00},
        {"gpt-3.5-turbo", "openai", 16385, 0.50, 2.00},
    };
    
    static model_info_t anthropic_models[] = {
        {"claude-3-5-sonnet-20241022", "anthropic", 200000, 3.00, 15.00},
        {"claude-3-opus-20240229", "anthropic", 200000, 15.00, 75.00},
        {"claude-3-sonnet-20240229", "anthropic", 200000, 3.00, 15.00},
        {"claude-3-haiku-20240307", "anthropic", 200000, 0.25, 1.25},
    };
    
    static model_info_t openrouter_models[] = {
        {"openai/gpt-4o", "openrouter", 128000, 2.50, 10.00},
        {"openai/gpt-4o-mini", "openrouter", 128000, 0.15, 0.60},
        {"anthropic/claude-3.5-sonnet", "openrouter", 200000, 3.00, 15.00},
        {"google/gemini-pro-1.5", "openrouter", 1000000, 1.25, 5.00},
        {"meta-llama/llama-3.1-70b-instruct", "openrouter", 128000, 0.80, 0.80},
    };
    
    static model_info_t ollama_models[] = {
        {"llama3.2", "ollama", 8192, 0.0, 0.0},
        {"llama3.1", "ollama", 32768, 0.0, 0.0},
        {"mistral", "ollama", 8192, 0.0, 0.0},
        {"codellama", "ollama", 16384, 0.0, 0.0},
        {"phi3", "ollama", 4096, 0.0, 0.0},
        {"qwen2.5", "ollama", 32768, 0.0, 0.0},
    };
    
    switch (type) {
        case PROVIDER_OPENAI:
        case PROVIDER_OPENAI_CODEX:
            *out_models = openai_models;
            *out_count = sizeof(openai_models) / sizeof(openai_models[0]);
            break;
        case PROVIDER_ANTHROPIC:
            *out_models = anthropic_models;
            *out_count = sizeof(anthropic_models) / sizeof(anthropic_models[0]);
            break;
        case PROVIDER_OPENROUTER:
            *out_models = openrouter_models;
            *out_count = sizeof(openrouter_models) / sizeof(openrouter_models[0]);
            break;
        case PROVIDER_OLLAMA:
            *out_models = ollama_models;
            *out_count = sizeof(ollama_models) / sizeof(ollama_models[0]);
            break;
        default:
            *out_models = NULL;
            *out_count = 0;
    }
    
    return 0;
}

int provider_list_available(provider_info_t **out_providers, size_t *out_count) {
    static provider_info_t providers[] = {
        {"openrouter", "OpenRouter", "https://openrouter.ai/api/v1", "", PROVIDER_OPENROUTER, true, false, NULL, 0},
        {"anthropic", "Anthropic (Claude)", "https://api.anthropic.com/v1", "", PROVIDER_ANTHROPIC, true, false, NULL, 0},
        {"openai", "OpenAI", "https://api.openai.com/v1", "", PROVIDER_OPENAI, true, false, NULL, 0},
        {"openai-codex", "OpenAI Codex", "https://api.openai.com/v1", "", PROVIDER_OPENAI_CODEX, true, false, NULL, 0},
        {"ollama", "Ollama (Local)", "http://localhost:11434", "", PROVIDER_OLLAMA, false, true, NULL, 0},
        {"gemini", "Google Gemini", "https://generativelanguage.googleapis.com/v1", "", PROVIDER_GEMINI, true, false, NULL, 0},
        {"glm", "Zhipu GLM", "https://open.bigmodel.cn/api/paas/v4", "", PROVIDER_GLM, true, false, NULL, 0},
        {"copilot", "GitHub Copilot", "https://api.github.com", "", PROVIDER_COPILOT, true, false, NULL, 0},
        {"llama", "llama.cpp (Local)", "", "", PROVIDER_LLAMA, false, true, NULL, 0},
    };
    
    *out_providers = providers;
    *out_count = sizeof(providers) / sizeof(providers[0]);
    return 0;
}

int provider_get_api_key(provider_type_t type, char *out_key, size_t out_len) {
    if (!out_key || out_len == 0) return -1;
    
    const char *env_var = NULL;
    
    switch (type) {
        case PROVIDER_OPENAI:
        case PROVIDER_OPENAI_CODEX:
            env_var = "OPENAI_API_KEY";
            break;
        case PROVIDER_ANTHROPIC:
            env_var = "ANTHROPIC_API_KEY";
            break;
        case PROVIDER_OPENROUTER:
            env_var = "OPENROUTER_API_KEY";
            break;
        case PROVIDER_GEMINI:
            env_var = "GEMINI_API_KEY";
            break;
        case PROVIDER_GLM:
            env_var = "ZHIPU_API_KEY";
            break;
        case PROVIDER_COPILOT:
            env_var = "GITHUB_TOKEN";
            break;
        default:
            return -1;
    }
    
    const char *key = getenv(env_var);
    if (key) {
        snprintf(out_key, out_len, "%s", key);
        return 0;
    }
    
    return -1;
}

int provider_router_init(provider_router_t *router, const provider_type_t *providers, size_t count, routing_strategy_t strategy) {
    if (!router || !providers || count == 0 || count > MAX_PROVIDERS) return -1;
    
    memset(router, 0, sizeof(provider_router_t));
    
    for (size_t i = 0; i < count; i++) {
        router->providers[i] = providers[i];
    }
    router->provider_count = count;
    router->strategy = strategy;
    router->current_index = 0;
    router->retry_count = 3;
    router->timeout_ms = 30000;
    
    return 0;
}

int provider_router_next(provider_router_t *router, provider_type_t *out_type) {
    if (!router || !out_type) return -1;
    
    switch (router->strategy) {
        case ROUTING_STRATEGY_ROUND_ROBIN:
            *out_type = router->providers[router->current_index];
            router->current_index = (router->current_index + 1) % router->provider_count;
            break;
            
        case ROUTING_STRATEGY_FAILOVER:
            *out_type = router->providers[0];
            break;
            
        case ROUTING_STRATEGY_LOAD_BALANCE:
            *out_type = router->providers[rand() % router->provider_count];
            break;
            
        case ROUTING_STRATEGY_COST_OPTIMIZED:
            *out_type = router->providers[router->provider_count - 1];
            break;
            
        default:
            *out_type = router->providers[0];
            break;
    }
    
    return 0;
}

int provider_router_select(provider_router_t *router, const char *model, provider_type_t *out_type) {
    if (!router || !model || !out_type) return -1;
    
    if (strstr(model, "gpt-4") || strstr(model, "claude-3-opus")) {
        *out_type = PROVIDER_OPENAI;
    } else if (strstr(model, "claude")) {
        *out_type = PROVIDER_ANTHROPIC;
    } else if (strstr(model, "gemini")) {
        *out_type = PROVIDER_GEMINI;
    } else {
        return provider_router_next(router, out_type);
    }
    
    return 0;
}

void provider_router_free(provider_router_t *router) {
    if (!router) return;
    memset(router, 0, sizeof(provider_router_t));
}

int provider_reliable_init(reliable_config_t *config, provider_type_t primary, provider_type_t fallback) {
    if (!config) return -1;
    
    memset(config, 0, sizeof(reliable_config_t));
    config->primary = primary;
    config->fallback = fallback;
    config->max_retries = 3;
    config->retry_delay_ms = 1000;
    config->auto_failover_enabled = true;
    
    return 0;
}

int provider_reliable_call(reliable_config_t *config, const char *model, const chat_context_t *context, chat_response_t *response) {
    if (!config || !model || !context || !response) return -1;
    
    int attempts = 0;
    int last_error = -1;
    
    while (attempts < config->max_retries + 1) {
        provider_type_t type = (attempts == 0) ? config->primary : config->fallback;
        
        last_error = provider_chat_completion(type, model, NULL, context, response);
        
        if (last_error == 0 && response->content[0] != '\0') {
            return 0;
        }
        
        if (!config->auto_failover_enabled || attempts >= config->max_retries) {
            break;
        }
        
        attempts++;
        
        if (config->retry_delay_ms > 0) {
            usleep(config->retry_delay_ms * 1000);
        }
    }
    
    snprintf(response->content, sizeof(response->content),
             "Error: All providers failed after %d attempts", attempts + 1);
    response->done = true;
    
    return last_error;
}

void provider_reliable_free(reliable_config_t *config) {
    if (!config) return;
    memset(config, 0, sizeof(reliable_config_t));
}

int provider_openai_compatible_call(const char *base_url, const char *api_key, const char *model, const chat_context_t *context, chat_response_t *response) {
    if (!base_url || !api_key || !model || !context || !response) return -1;
    
    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", base_url);
    
    char auth_header[512];
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    
    char body[16384];
    char messages_json[8192];
    build_messages_json(context, messages_json, sizeof(messages_json));
    
    snprintf(body, sizeof(body),
             "{\"model\":\"%s\",\"messages\":[%s],\"temperature\":%f,\"max_tokens\":%zu}",
             model, messages_json, context->temperature, context->max_tokens);
    
    const char *headers[] = {auth_header, "Content-Type: application/json"};
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 2, body, &resp);
    
    if (result == 0 && resp.data) {
        parse_response_content(resp.data, response->content, sizeof(response->content));
        response->done = true;
    }
    
    http_response_free(&resp);
    return result;
}

int provider_anthropic_call(const char *api_key, const char *model, const chat_context_t *context, chat_response_t *response) {
    return provider_anthropic_completion(api_key, model, context, response);
}

int provider_gemini_call(const char *api_key, const char *model, const chat_context_t *context, chat_response_t *response) {
    return provider_gemini_completion(api_key, model, context, response);
}

int provider_openai_with_tools(const char *api_key, const char *model, const chat_context_t *context, const char *tools_json, chat_response_t *response) {
    if (!api_key || !context || !response) return -1;
    
    char url[512];
    char auth_header[512];
    char body[32768];
    char messages_json[8192];
    
    snprintf(url, sizeof(url), "https://api.openai.com/v1/chat/completions");
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
    
    build_messages_json(context, messages_json, sizeof(messages_json));
    
    if (tools_json && strlen(tools_json) > 0) {
        snprintf(body, sizeof(body),
                 "{\"model\":\"%s\",\"messages\":[%s],\"tools\":%s,\"temperature\":%f,\"max_tokens\":%zu,\"stream\":false}",
                 model ? model : "gpt-4o", messages_json, tools_json, 
                 context->temperature, context->max_tokens);
    } else {
        snprintf(body, sizeof(body),
                 "{\"model\":\"%s\",\"messages\":[%s],\"temperature\":%f,\"max_tokens\":%zu,\"stream\":false}",
                 model ? model : "gpt-4o", messages_json, 
                 context->temperature, context->max_tokens);
    }
    
    const char *headers[] = {auth_header, "Content-Type: application/json", "Accept: application/json"};
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 3, body, &resp);
    
    if (result == 0 && resp.data) {
        parse_response_content(resp.data, response->content, sizeof(response->content));
        response->done = true;
        
        if (strstr(resp.data, "\"tool_calls\"") != NULL) {
            response->tool_calls.call_count = 0;
            
            char *tool_calls_start = strstr(resp.data, "\"tool_calls\"");
            if (tool_calls_start) {
                char *arr_start = strchr(tool_calls_start, '[');
                if (arr_start) {
                    char *arr_end = strchr(arr_start, ']');
                    if (arr_end) {
                        size_t arr_len = arr_end - arr_start + 1;
                        if (arr_len < sizeof(response->content)) {
                            char tool_calls_json[4096] = {0};
                            strncpy(tool_calls_json, arr_start, arr_len);
                            
                            int call_idx = 0;
                            char *call_start = strchr(arr_start, '{');
                            while (call_start && call_idx < MAX_TOOL_CALLS && call_start < arr_end) {
                                char *call_end = strchr(call_start, '}');
                                if (call_end) {
                                    size_t call_len = call_end - call_start + 1;
                                    if (call_len < sizeof(response->tool_calls.calls[call_idx].arguments)) {
                                        char call_json[4096] = {0};
                                        strncpy(call_json, call_start, call_len);
                                        
                                        char func_name[128] = {0};
                                        
                                        if (find_json_value(call_json, "function", func_name, sizeof(func_name)) == 0) {
                                            if (strstr(func_name, "{")) {
                                                char *args_start = strchr(func_name, '{');
                                                if (args_start) {
                                                    strncpy(response->tool_calls.calls[call_idx].arguments, args_start, 
                                                           sizeof(response->tool_calls.calls[call_idx].arguments) - 1);
                                                }
                                            }
                                            char *name_start = strchr(func_name, '"');
                                            if (name_start) {
                                                name_start++;
                                                char *name_end = strchr(name_start, '"');
                                                if (name_end) {
                                                    size_t name_len = name_end - name_start;
                                                    if (name_len < sizeof(response->tool_calls.calls[call_idx].name)) {
                                                        strncpy(response->tool_calls.calls[call_idx].name, name_start, name_len);
                                                    }
                                                }
                                            }
                                            response->tool_calls.call_count++;
                                            call_idx++;
                                        }
                                    }
                                    call_start = strchr(call_end + 1, '{');
                                } else {
                                    break;
                                }
                            }
                        }
                    }
                }
            }
        }
        
        char usage[64];
        if (find_json_value(resp.data, "usage", usage, sizeof(usage)) == 0) {
            char prompt_tokens[32], completion_tokens[32], total_tokens[32];
            if (find_json_value(usage, "prompt_tokens", prompt_tokens, sizeof(prompt_tokens)) == 0) {
                response->prompt_tokens = atoi(prompt_tokens);
            }
            if (find_json_value(usage, "completion_tokens", completion_tokens, sizeof(completion_tokens)) == 0) {
                response->completion_tokens = atoi(completion_tokens);
            }
            if (find_json_value(usage, "total_tokens", total_tokens, sizeof(total_tokens)) == 0) {
                response->tokens_used = atoi(total_tokens);
            }
        }
    } else {
        snprintf(response->content, sizeof(response->content), "HTTP request failed");
    }
    
    http_response_free(&resp);
    return result;
}

int provider_anthropic_with_tools(const char *api_key, const char *model, const chat_context_t *context, const char *tools_json, chat_response_t *response) {
    if (!api_key || !context || !response) return -1;
    
    char url[] = "https://api.anthropic.com/v1/messages";
    char auth_header[512];
    char version_header[] = "anthropic-version: 2023-06-01";
    char body[32768];
    char messages_json[8192];
    
    snprintf(auth_header, sizeof(auth_header), "x-api-key: %s", api_key);
    
    build_messages_json(context, messages_json, sizeof(messages_json));
    
    if (tools_json && strlen(tools_json) > 0) {
        snprintf(body, sizeof(body),
                 "{\"model\":\"%s\",\"messages\":[%s],\"tools\":%s,\"temperature\":%f,\"max_tokens\":%zu}",
                 model ? model : "claude-3-5-sonnet-20241022", messages_json, tools_json,
                 context->temperature, context->max_tokens);
    } else {
        snprintf(body, sizeof(body),
                 "{\"model\":\"%s\",\"messages\":[%s],\"temperature\":%f,\"max_tokens\":%zu}",
                 model ? model : "claude-3-5-sonnet-20241022", messages_json,
                 context->temperature, context->max_tokens);
    }
    
    const char *headers[] = {auth_header, version_header, "Content-Type: application/json"};
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 3, body, &resp);
    
    if (result == 0 && resp.data) {
        find_json_value(resp.data, "content", response->content, sizeof(response->content));
        response->done = true;
        
        if (strstr(resp.data, "\"type\":\"tool_use\"") != NULL) {
            response->tool_calls.call_count = 0;
            
            char *content_start = strstr(resp.data, "\"content\"");
            if (content_start) {
                char *arr_start = strchr(content_start, '[');
                if (arr_start) {
                    char *arr_end = strchr(arr_start, ']');
                    if (arr_end) {
                        char *block_start = strchr(arr_start, '{');
                        int call_idx = 0;
                        while (block_start && call_idx < MAX_TOOL_CALLS && block_start < arr_end) {
                            char *block_end = strchr(block_start, '}');
                            if (block_end) {
                                size_t block_len = block_end - block_start + 1;
                                if (block_len < sizeof(response->content)) {
                                    char block_json[4096] = {0};
                                    strncpy(block_json, block_start, block_len);
                                    
                                    char tool_id[128] = {0};
                                    char tool_name[128] = {0};
                                    char tool_input[4096] = {0};
                                    
                                    find_json_value(block_json, "id", tool_id, sizeof(tool_id));
                                    find_json_value(block_json, "name", tool_name, sizeof(tool_name));
                                    find_json_value(block_json, "input", tool_input, sizeof(tool_input));
                                    
                                    if (tool_name[0]) {
                                        snprintf(response->tool_calls.calls[call_idx].name, 
                                                 sizeof(response->tool_calls.calls[call_idx].name), "%s", tool_name);
                                        snprintf(response->tool_calls.calls[call_idx].arguments,
                                                 sizeof(response->tool_calls.calls[call_idx].arguments), "%s", tool_input);
                                        response->tool_calls.call_count++;
                                        call_idx++;
                                    }
                                }
                                block_start = strchr(block_end + 1, '{');
                            } else {
                                break;
                            }
                        }
                    }
                }
            }
        }
        
        char thinking[4096];
        if (find_json_value(resp.data, "thinking", thinking, sizeof(thinking)) == 0) {
            snprintf(response->reasoning, sizeof(response->reasoning), "%s", thinking);
        }
        
        char usage[64];
        if (find_json_value(resp.data, "usage", usage, sizeof(usage)) == 0) {
            char input_tokens[32], output_tokens[32];
            if (find_json_value(usage, "input_tokens", input_tokens, sizeof(input_tokens)) == 0) {
                response->prompt_tokens = atoi(input_tokens);
            }
            if (find_json_value(usage, "output_tokens", output_tokens, sizeof(output_tokens)) == 0) {
                response->completion_tokens = atoi(output_tokens);
                response->tokens_used = response->prompt_tokens + response->completion_tokens;
            }
        }
    } else {
        snprintf(response->content, sizeof(response->content), "HTTP request failed");
    }
    
    http_response_free(&resp);
    return result;
}

int provider_chat_with_tools(
    provider_type_t type,
    const char *model,
    const char *api_key,
    const chat_context_t *context,
    const char *tools_json,
    chat_response_t *response
) {
    if (!response || !context) return -1;
    
    memset(response, 0, sizeof(chat_response_t));
    
    if (!api_key) {
        api_key = getenv("OPENAI_API_KEY");
        if (!api_key) {
            api_key = getenv("ANTHROPIC_API_KEY");
        }
    }
    
    if (!api_key) {
        snprintf(response->content, sizeof(response->content),
                 "Error: No API key configured");
        response->done = true;
        return -1;
    }
    
    switch (type) {
        case PROVIDER_OPENAI:
        case PROVIDER_OPENAI_CODEX:
        case PROVIDER_OPENROUTER:
            return provider_openai_with_tools(
                api_key,
                model[0] ? model : "gpt-4o",
                context,
                tools_json,
                response
            );
            
        case PROVIDER_ANTHROPIC:
            return provider_anthropic_with_tools(
                api_key,
                model[0] ? model : "claude-3-5-sonnet-20241022",
                context,
                tools_json,
                response
            );
            
        default:
            snprintf(response->content, sizeof(response->content), 
                     "Error: Tool calling not supported for this provider");
            response->done = true;
            return -1;
    }
}
