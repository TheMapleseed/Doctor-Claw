#ifndef DOCTORCLAW_UTIL_H
#define DOCTORCLAW_UTIL_H

#include "c23_check.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdbool.h>

#define JSON_MAX_DEPTH 16
#define JSON_MAX_STRING_LEN 4096
#define JSON_MAX_KEYS 64

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef struct json_value_s json_value_t;

struct json_value_s {
    json_type_t type;
    union {
        bool bool_value;
        double number_value;
        char string_value[JSON_MAX_STRING_LEN];
        struct {
            json_value_t *values;
            size_t count;
        } array;
        struct {
            char keys[JSON_MAX_KEYS][64];
            json_value_t *values;
            size_t count;
        } object;
    } value;
};

typedef struct {
    json_value_t root;
    const char *error;
    size_t error_pos;
} json_parser_t;

void util_trim_whitespace(char *str);
int util_hex_encode(const unsigned char *input, size_t len, char *output);
int util_hex_decode(const char *input, unsigned char *output, size_t *out_len);
char *util_strdup(const char *s);

int util_base64_encode(const unsigned char *input, size_t len, char *output, size_t output_size);
int util_base64_decode(const char *input, unsigned char *output, size_t *out_len);

int util_json_init(json_parser_t *parser, const char *json_str);
json_value_t *util_json_get(json_value_t *obj, const char *path);
const char *util_json_get_string(json_value_t *obj, const char *key, const char *default_value);
double util_json_get_number(json_value_t *obj, const char *key, double default_value);
bool util_json_get_bool(json_value_t *obj, const char *key, bool default_value);
void util_json_free(json_parser_t *parser);

int util_url_encode(const char *input, char *output, size_t output_size);
int util_url_decode(const char *input, char *output, size_t output_size);

uint64_t util_timestamp_ms(void);
void util_sleep_ms(uint64_t ms);

int util_random_bytes(unsigned char *output, size_t len);

char *util_strjoin(const char *separator, ...);

int util_hmac_sha256(const char *key, size_t key_len, const char *message, size_t msg_len, char *output);
int util_webhook_verify_telegram(const char *secret, const char *body, const char *signature);
int util_webhook_verify_discord(const char *secret, const char *body, const char *signature);
int util_webhook_verify_slack(const char *secret, const char *body, const char *signature);

typedef struct {
    char ip[48];
    uint64_t request_count;
    uint64_t first_request_time;
    uint64_t last_request_time;
} rate_limit_entry_t;

typedef struct {
    rate_limit_entry_t entries[256];
    size_t entry_count;
    uint64_t window_ms;
    size_t max_requests;
} rate_limiter_t;

int rate_limiter_init(rate_limiter_t *limiter, uint64_t window_ms, size_t max_requests);
int rate_limiter_check(rate_limiter_t *limiter, const char *ip);
void rate_limiter_cleanup(rate_limiter_t *limiter);
void rate_limiter_free(rate_limiter_t *limiter);

#endif
