#ifndef DOCTORCLAW_CHANNELS_H
#define DOCTORCLAW_CHANNELS_H

#include "c23_check.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_CHANNEL_NAME 64
#define MAX_CHANNEL_TOKEN 512
#define MAX_MESSAGE_CONTENT 16384

typedef enum {
    CHANNEL_TELEGRAM,
    CHANNEL_DISCORD,
    CHANNEL_SLACK,
    CHANNEL_WHATSAPP,
    CHANNEL_SIGNAL,
    CHANNEL_MATRIX,
    CHANNEL_IRC,
    CHANNEL_EMAIL,
    CHANNEL_DINGTALK,
    CHANNEL_LARK,
    CHANNEL_MATTERMOST,
    CHANNEL_QQ,
    CHANNEL_WEBHOOK,
    CHANNEL_IMESSAGE,
    CHANNEL_CLI,
    CHANNEL_UNKNOWN
} channel_type_t;

typedef struct {
    char name[MAX_CHANNEL_NAME];
    channel_type_t type;
    char bot_token[MAX_CHANNEL_TOKEN];
    char api_key[MAX_CHANNEL_TOKEN];
    char webhook_url[MAX_CHANNEL_TOKEN];
    bool enabled;
    char *allowed_users[32];
    size_t allowed_user_count;
} channel_config_t;

typedef struct {
    char sender[128];
    char content[MAX_MESSAGE_CONTENT];
    uint64_t timestamp;
} channel_message_t;

typedef struct {
    channel_type_t type;
    void *handle;
    bool connected;
} channel_t;

typedef int (*channel_message_handler_t)(const channel_message_t *msg, void *user_data);

channel_type_t channel_get_type(const char *name);
const char* channel_get_name(channel_type_t type);

int channel_init(channel_t *ch, channel_type_t type, const channel_config_t *config);
int channel_connect(channel_t *ch);
int channel_disconnect(channel_t *ch);
int channel_send_message(channel_t *ch, const char *content);
int channel_send_message_to(channel_t *ch, const char *target, const char *content);
int channel_list_configured(channel_config_t **out_configs, size_t *out_count);
int channel_start_all(void);
int channel_health_check(channel_type_t type);

void channel_set_message_handler(channel_t *ch, channel_message_handler_t handler, void *user_data);
int channel_process_message(channel_t *ch, const char *json_payload);
int channel_get_message(channel_t *ch, channel_message_t *out_message);

int channels_load_config(const char *config_path);
int channels_save_config(const char *config_path);

int channel_add_allowed_user(channel_t *ch, const char *user_id);
int channel_remove_allowed_user(channel_t *ch, const char *user_id);
bool channel_is_user_allowed(channel_t *ch, const char *user_id);

/** Reply to an incoming webhook (parse path+body, send response_text to the channel). */
int channels_reply_to_webhook(const char *path, const char *body, const char *response_text);

/** Start background listeners (e.g. Telegram getUpdates). jobcache is jobcache_t* for enqueue. */
int channel_start_listeners(const char *config_path, void *jobcache);
/** Stop listeners. */
void channel_stop_listeners(void);

#endif
