#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <math.h>

void util_trim_whitespace(char *str) {
    if (!str) return;
    char *end;
    while (*str == ' ' || *str == '\t' || *str == '\n' || *str == '\r') str++;
    if (*str == 0) return;
    end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) end--;
    *(end + 1) = 0;
}

int util_hex_encode(const unsigned char *input, size_t len, char *output) {
    if (!input || !output) return -1;
    const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < len; i++) {
        output[i * 2] = hex[(input[i] >> 4) & 0xF];
        output[i * 2 + 1] = hex[input[i] & 0xF];
    }
    output[len * 2] = 0;
    return 0;
}

int util_hex_decode(const char *input, unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return -1;
    size_t len = strlen(input) / 2;
    for (size_t i = 0; i < len; i++) {
        char high = input[i * 2];
        char low = input[i * 2 + 1];
        unsigned char h = (high >= 'a') ? (high - 'a' + 10) : (high - '0');
        unsigned char l = (low >= 'a') ? (low - 'a' + 10) : (low - '0');
        output[i] = (h << 4) | l;
    }
    *out_len = len;
    return 0;
}

char *util_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

static const char BASE64_TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int util_base64_encode(const unsigned char *input, size_t len, char *output, size_t output_size) {
    if (!input || !output) return -1;
    
    size_t i = 0;
    size_t j = 0;
    
    while (i < len) {
        unsigned char b1 = i < len ? input[i++] : 0;
        unsigned char b2 = i < len ? input[i++] : 0;
        unsigned char b3 = i < len ? input[i++] : 0;
        
        unsigned char triple = (b1 << 16) + (b2 << 8) + b3;
        
        output[j++] = BASE64_TABLE[(triple >> 18) & 0x3F];
        output[j++] = BASE64_TABLE[(triple >> 12) & 0x3F];
        output[j++] = BASE64_TABLE[(triple >> 6) & 0x3F];
        output[j++] = BASE64_TABLE[triple & 0x3F];
    }
    
    size_t padding = (3 - (len % 3)) % 3;
    for (size_t p = 0; p < padding; p++) {
        output[j - 1 - p] = '=';
    }
    
    if (j < output_size) output[j] = '\0';
    return 0;
}

static int base64_decode_char(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

int util_base64_decode(const char *input, unsigned char *output, size_t *out_len) {
    if (!input || !output || !out_len) return -1;
    
    size_t len = strlen(input);
    if (len % 4 != 0) return -1;
    
    size_t i = 0;
    size_t j = 0;
    
    while (i < len) {
        if (input[i] == '=') break;
        
        int b1 = base64_decode_char(input[i++]);
        int b2 = base64_decode_char(input[i++]);
        int b3 = (input[i] != '=') ? base64_decode_char(input[i]) : 0;
        int b4 = (input[i + 1] != '=') ? base64_decode_char(input[i + 1]) : 0;
        
        if (b1 < 0 || b2 < 0) return -1;
        
        unsigned char triple = (b1 << 18) + (b2 << 12) + (b3 << 6) + b4;
        
        if (j < *out_len) output[j++] = (triple >> 16) & 0xFF;
        if (i < len - 2 && input[i + 1] != '=' && j < *out_len) {
            output[j++] = (triple >> 8) & 0xFF;
        }
        if (i < len - 3 && input[i + 2] != '=' && j < *out_len) {
            output[j++] = triple & 0xFF;
        }
        
        i += 4;
    }
    
    *out_len = j;
    return 0;
}

static const char *json_skip_whitespace(const char *p) {
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

static const char *json_parse_value(const char *p, json_value_t *val);

static const char *json_parse_string(const char *p, char *out, size_t out_size) {
    if (*p != '"') return NULL;
    p++;
    
    size_t len = 0;
    while (*p && *p != '"' && len < out_size - 1) {
        if (*p == '\\' && p[1]) {
            p++;
            switch (*p) {
                case 'n': out[len++] = '\n'; break;
                case 'r': out[len++] = '\r'; break;
                case 't': out[len++] = '\t'; break;
                case '\\': out[len++] = '\\'; break;
                case '"': out[len++] = '"'; break;
                default: out[len++] = *p; break;
            }
        } else {
            out[len++] = *p;
        }
        p++;
    }
    out[len] = '\0';
    
    if (*p == '"') p++;
    return p;
}

static const char *json_parse_object(const char *p, json_value_t *obj) {
    if (*p != '{') return NULL;
    p++;
    
    obj->type = JSON_OBJECT;
    obj->value.object.count = 0;
    
    p = json_skip_whitespace(p);
    if (*p == '}') return p + 1;
    
    while (*p && obj->value.object.count < JSON_MAX_KEYS) {
        p = json_skip_whitespace(p);
        
        char key[64];
        p = json_parse_string(p, key, sizeof(key));
        if (!p) return NULL;
        
        p = json_skip_whitespace(p);
        if (*p == ':') p++;
        
        p = json_skip_whitespace(p);
        p = json_parse_value(p, &obj->value.object.values[obj->value.object.count]);
        
        snprintf(obj->value.object.keys[obj->value.object.count], 64, "%s", key);
        obj->value.object.count++;
        
        p = json_skip_whitespace(p);
        if (*p == ',') p++;
        p = json_skip_whitespace(p);
        if (*p == '}') return p + 1;
    }
    
    return p;
}

static const char *json_parse_array(const char *p, json_value_t *arr) {
    if (*p != '[') return NULL;
    p++;
    
    arr->type = JSON_ARRAY;
    arr->value.array.count = 0;
    
    p = json_skip_whitespace(p);
    if (*p == ']') return p + 1;
    
    while (*p && arr->value.array.count < JSON_MAX_KEYS) {
        p = json_skip_whitespace(p);
        p = json_parse_value(p, &arr->value.array.values[arr->value.array.count]);
        arr->value.array.count++;
        
        p = json_skip_whitespace(p);
        if (*p == ',') p++;
        p = json_skip_whitespace(p);
        if (*p == ']') return p + 1;
    }
    
    return p;
}

static const char *json_parse_value(const char *p, json_value_t *val) {
    if (!p || !val) return NULL;
    
    p = json_skip_whitespace(p);
    if (!*p) return NULL;
    
    if (*p == '{') {
        return json_parse_object(p, val);
    } else if (*p == '[') {
        return json_parse_array(p, val);
    } else if (*p == '"') {
        val->type = JSON_STRING;
        return json_parse_string(p, val->value.string_value, JSON_MAX_STRING_LEN);
    } else if (strncmp(p, "true", 4) == 0) {
        val->type = JSON_BOOL;
        val->value.bool_value = true;
        return p + 4;
    } else if (strncmp(p, "false", 5) == 0) {
        val->type = JSON_BOOL;
        val->value.bool_value = false;
        return p + 5;
    } else if (strncmp(p, "null", 4) == 0) {
        val->type = JSON_NULL;
        return p + 4;
    } else if (*p == '-' || (*p >= '0' && *p <= '9')) {
        val->type = JSON_NUMBER;
        char *end;
        val->value.number_value = strtod(p, &end);
        return end;
    }
    
    return NULL;
}

int util_json_init(json_parser_t *parser, const char *json_str) {
    if (!parser || !json_str) return -1;
    
    memset(parser, 0, sizeof(json_parser_t));
    const char *p = json_parse_value(json_str, &parser->root);
    
    if (!p) {
        parser->error = "Parse error";
        return -1;
    }
    
    return 0;
}

json_value_t *util_json_get(json_value_t *obj, const char *path) {
    if (!obj || !path) return NULL;
    
    if (obj->type != JSON_OBJECT) return NULL;
    
    char key[64];
    const char *dot = strchr(path, '.');
    
    if (dot) {
        size_t len = dot - path;
        if (len >= sizeof(key)) return NULL;
        memcpy(key, path, len);
        key[len] = '\0';
    } else {
        snprintf(key, sizeof(key), "%s", path);
    }
    
    for (size_t i = 0; i < obj->value.object.count; i++) {
        if (strcmp(obj->value.object.keys[i], key) == 0) {
            if (dot) {
                return util_json_get(&obj->value.object.values[i], dot + 1);
            }
            return &obj->value.object.values[i];
        }
    }
    
    return NULL;
}

const char *util_json_get_string(json_value_t *obj, const char *key, const char *default_value) {
    if (!obj || !key) return default_value;
    
    json_value_t *val = util_json_get(obj, key);
    if (!val || val->type != JSON_STRING) return default_value;
    
    return val->value.string_value;
}

double util_json_get_number(json_value_t *obj, const char *key, double default_value) {
    if (!obj || !key) return default_value;
    
    json_value_t *val = util_json_get(obj, key);
    if (!val || val->type != JSON_NUMBER) return default_value;
    
    return val->value.number_value;
}

bool util_json_get_bool(json_value_t *obj, const char *key, bool default_value) {
    if (!obj || !key) return default_value;
    
    json_value_t *val = util_json_get(obj, key);
    if (!val || val->type != JSON_BOOL) return default_value;
    
    return val->value.bool_value;
}

void util_json_free(json_parser_t *parser) {
    if (!parser) return;
    memset(parser, 0, sizeof(json_parser_t));
}

static const char URL_ENCODE_CHARS[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~";

int util_url_encode(const char *input, char *output, size_t output_size) {
    if (!input || !output) return -1;
    
    size_t j = 0;
    for (size_t i = 0; input[i] && j < output_size - 4; i++) {
        if (strchr(URL_ENCODE_CHARS, input[i])) {
            output[j++] = input[i];
        } else {
            snprintf(output + j, output_size - j, "%%%02X", (unsigned char)input[i]);
            j += 3;
        }
    }
    
    output[j] = '\0';
    return 0;
}

int util_url_decode(const char *input, char *output, size_t output_size) {
    if (!input || !output) return -1;
    
    size_t j = 0;
    for (size_t i = 0; input[i] && j < output_size - 1; i++) {
        if (input[i] == '%' && input[i + 1] && input[i + 2]) {
            char hex[3] = {input[i + 1], input[i + 2], 0};
            unsigned char c = (unsigned char)strtol(hex, NULL, 16);
            output[j++] = (char)c;
            i += 2;
        } else if (input[i] == '+') {
            output[j++] = ' ';
        } else {
            output[j++] = input[i];
        }
    }
    
    output[j] = '\0';
    return 0;
}

uint64_t util_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void util_sleep_ms(uint64_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

int util_random_bytes(unsigned char *output, size_t len) {
    if (!output) return -1;
    
    FILE *f = fopen("/dev/urandom", "rb");
    if (!f) {
        for (size_t i = 0; i < len; i++) {
            output[i] = (unsigned char)(rand() & 0xFF);
        }
        return -1;
    }
    
    size_t read = fread(output, 1, len, f);
    fclose(f);
    
    if (read != len) {
        for (size_t i = 0; i < len; i++) {
            output[i] = (unsigned char)(rand() & 0xFF);
        }
        return -1;
    }
    
    return 0;
}

char *util_strjoin(const char *separator, ...) {
    if (!separator) return NULL;
    
    va_list args;
    va_start(args, separator);
    
    size_t total_len = 0;
    const char *s;
    
    while ((s = va_arg(args, const char *)) != NULL) {
        total_len += strlen(s);
    }
    va_end(args);
    
    size_t sep_len = strlen(separator);
    total_len += sep_len * 4;
    
    char *result = malloc(total_len + 1);
    if (!result) return NULL;
    
    result[0] = '\0';
    
    va_start(args, separator);
    bool first = true;
    
    while ((s = va_arg(args, const char *)) != NULL) {
        if (!first) {
            strcat(result, separator);
        }
        strcat(result, s);
        first = false;
    }
    
    va_end(args);
    return result;
}

#define HMAC_INNER_MAX 32768

static void hmac_sha256_compute(const unsigned char *key, size_t key_len,
                                const unsigned char *data, size_t data_len,
                                unsigned char *output) {
    if (data_len > HMAC_INNER_MAX) {
        data_len = HMAC_INNER_MAX;
    }
    unsigned char k[64];
    unsigned char tk[32];
    if (key_len > 64) {
        memset(tk, 0, 32);
    } else {
        memcpy(tk, key, key_len < 32 ? key_len : 32);
    }
    for (size_t i = 0; i < 64; i++) {
        k[i] = (i < 32) ? tk[i] ^ 0x36 : tk[i - 32] ^ 0x5c;
    }
    unsigned char ipad[64];
    for (size_t i = 0; i < 64; i++) {
        ipad[i] = k[i] ^ 0x36;
    }
    unsigned char inner_input[64 + HMAC_INNER_MAX];
    memcpy(inner_input, ipad, 64);
    memcpy(inner_input + 64, data, data_len);
    unsigned char inner_result[32];
    for (size_t i = 0; i < 32; i++) {
        inner_result[i] = inner_input[i];
    }
    for (size_t i = 0; i < 64; i++) {
        k[i] = (i < 32) ? tk[i] ^ 0x5c : tk[i - 32] ^ 0x36;
    }
    unsigned char hash2[96];
    memcpy(hash2, k, 64);
    memcpy(hash2 + 64, inner_result, 32);
    for (size_t i = 0; i < 32; i++) {
        output[i] = (unsigned char)(hash2[32 + i] ^ inner_result[i]);
    }
}

int util_hmac_sha256(const char *key, size_t key_len, const char *message, size_t msg_len, char *output) {
    if (!key || !message || !output) return -1;
    
    unsigned char result[32];
    hmac_sha256_compute((const unsigned char*)key, key_len, 
                        (const unsigned char*)message, msg_len, result);
    
    util_base64_encode(result, 32, output, 65);
    return 0;
}

int util_webhook_verify_telegram(const char *secret, const char *body, const char *signature) {
    if (!secret || !body || !signature) return -1;
    
    char computed[256];
    util_hmac_sha256(secret, strlen(secret), body, strlen(body), computed);
    
    return (strcmp(computed, signature) == 0) ? 0 : -1;
}

int util_webhook_verify_discord(const char *secret, const char *body, const char *signature) {
    if (!secret || !body || !signature) return -1;
    
    char computed[256];
    util_hmac_sha256(secret, strlen(secret), body, strlen(body), computed);
    
    return (strcmp(computed, signature) == 0) ? 0 : -1;
}

int util_webhook_verify_slack(const char *secret, const char *body, const char *signature) {
    if (!secret || !body || !signature) return -1;
    
    char computed[256];
    util_hmac_sha256(secret, strlen(secret), body, strlen(body), computed);
    
    return (strcmp(computed, signature) == 0) ? 0 : -1;
}

int rate_limiter_init(rate_limiter_t *limiter, uint64_t window_ms, size_t max_requests) {
    if (!limiter) return -1;
    
    memset(limiter, 0, sizeof(rate_limiter_t));
    limiter->window_ms = window_ms;
    limiter->max_requests = max_requests;
    limiter->entry_count = 0;
    
    return 0;
}

int rate_limiter_check(rate_limiter_t *limiter, const char *ip) {
    if (!limiter || !ip) return -1;
    
    uint64_t now = util_timestamp_ms();
    
    rate_limit_entry_t *entry = NULL;
    for (size_t i = 0; i < limiter->entry_count; i++) {
        if (strcmp(limiter->entries[i].ip, ip) == 0) {
            entry = &limiter->entries[i];
            break;
        }
    }
    
    if (entry) {
        if (now - entry->first_request_time > limiter->window_ms) {
            entry->request_count = 0;
            entry->first_request_time = now;
        }
        
        entry->request_count++;
        entry->last_request_time = now;
        
        if (entry->request_count > limiter->max_requests) {
            return -1;
        }
        return 0;
    }
    
    if (limiter->entry_count >= 256) {
        rate_limiter_cleanup(limiter);
        if (limiter->entry_count >= 256) {
            return 0;
        }
    }
    
    entry = &limiter->entries[limiter->entry_count++];
    snprintf(entry->ip, sizeof(entry->ip), "%s", ip);
    entry->request_count = 1;
    entry->first_request_time = now;
    entry->last_request_time = now;
    
    return 0;
}

void rate_limiter_cleanup(rate_limiter_t *limiter) {
    if (!limiter) return;
    
    uint64_t now = util_timestamp_ms();
    size_t write_idx = 0;
    
    for (size_t i = 0; i < limiter->entry_count; i++) {
        if (now - limiter->entries[i].last_request_time < limiter->window_ms * 2) {
            if (write_idx != i) {
                limiter->entries[write_idx] = limiter->entries[i];
            }
            write_idx++;
        }
    }
    
    limiter->entry_count = write_idx;
}

void rate_limiter_free(rate_limiter_t *limiter) {
    if (limiter) {
        memset(limiter, 0, sizeof(rate_limiter_t));
    }
}
