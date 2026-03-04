#include "channels.h"
#include "providers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <curl/curl.h>
#include <unistd.h>

#define MAX_CHANNELS 32

typedef struct {
    channel_config_t config;
    bool active;
    time_t last_health_check;
} channel_instance_t;

static channel_instance_t g_channels[MAX_CHANNELS];
static size_t g_channel_count = 0;
static channel_message_handler_t g_message_handler = NULL;
static void *g_message_user_data = NULL;

channel_type_t channel_get_type(const char *name) {
    if (strcmp(name, "telegram") == 0) return CHANNEL_TELEGRAM;
    if (strcmp(name, "discord") == 0) return CHANNEL_DISCORD;
    if (strcmp(name, "slack") == 0) return CHANNEL_SLACK;
    if (strcmp(name, "whatsapp") == 0) return CHANNEL_WHATSAPP;
    if (strcmp(name, "signal") == 0) return CHANNEL_SIGNAL;
    if (strcmp(name, "matrix") == 0) return CHANNEL_MATRIX;
    if (strcmp(name, "irc") == 0) return CHANNEL_IRC;
    if (strcmp(name, "email") == 0) return CHANNEL_EMAIL;
    if (strcmp(name, "dingtalk") == 0) return CHANNEL_DINGTALK;
    if (strcmp(name, "lark") == 0) return CHANNEL_LARK;
    if (strcmp(name, "mattermost") == 0) return CHANNEL_MATTERMOST;
    if (strcmp(name, "qq") == 0) return CHANNEL_QQ;
    if (strcmp(name, "webhook") == 0) return CHANNEL_WEBHOOK;
    if (strcmp(name, "imessage") == 0) return CHANNEL_IMESSAGE;
    if (strcmp(name, "cli") == 0) return CHANNEL_CLI;
    return CHANNEL_UNKNOWN;
}

const char* channel_get_name(channel_type_t type) {
    switch (type) {
        case CHANNEL_TELEGRAM: return "telegram";
        case CHANNEL_DISCORD: return "discord";
        case CHANNEL_SLACK: return "slack";
        case CHANNEL_WHATSAPP: return "whatsapp";
        case CHANNEL_SIGNAL: return "signal";
        case CHANNEL_MATRIX: return "matrix";
        case CHANNEL_IRC: return "irc";
        case CHANNEL_EMAIL: return "email";
        case CHANNEL_DINGTALK: return "dingtalk";
        case CHANNEL_LARK: return "lark";
        case CHANNEL_MATTERMOST: return "mattermost";
        case CHANNEL_QQ: return "qq";
        case CHANNEL_WEBHOOK: return "webhook";
        case CHANNEL_IMESSAGE: return "imessage";
        case CHANNEL_CLI: return "cli";
        default: return "unknown";
    }
}

int channel_init(channel_t *ch, channel_type_t type, const channel_config_t *config) {
    if (!ch || !config) return -1;
    
    memset(ch, 0, sizeof(channel_t));
    ch->type = type;
    ch->handle = NULL;
    ch->connected = false;
    
    if (g_channel_count < MAX_CHANNELS) {
        memcpy(&g_channels[g_channel_count].config, config, sizeof(channel_config_t));
        g_channels[g_channel_count].active = false;
        g_channels[g_channel_count].last_health_check = 0;
        g_channel_count++;
    }
    
    return 0;
}

#define MAX_MESSAGE_LENGTH 4096

static int telegram_send_typing(const channel_config_t *config, int typing) {
    char url[512];
    char body[512];
    
    snprintf(url, sizeof(url), 
             "https://api.telegram.org/bot%s/sendChatAction",
             config->bot_token);
    
    snprintf(body, sizeof(body),
             "{\"chat_id\":\"%s\",\"action\":\"%s\"}",
             config->allowed_users[0] ? config->allowed_users[0] : "",
             typing ? "typing" : "cancel");
    
    http_response_t resp = {0};
    const char *headers[] = {"Content-Type: application/json"};
    int result = http_post(url, headers, 1, body, &resp);
    http_response_free(&resp);
    return result;
}

static char* strip_tool_calls(const char *content) {
    static char cleaned[8192];
    size_t j = 0;
    
    for (size_t i = 0; content[i] && j < sizeof(cleaned) - 1; i++) {
        if (content[i] == '<' && 
            (strncmp(content + i, "<tool_call>", 10) == 0 ||
             strncmp(content + i, "</tool_call>", 11) == 0 ||
             strncmp(content + i, "<tool_code>", 10) == 0)) {
            while (content[i] && content[i] != '>') i++;
            continue;
        }
        cleaned[j++] = content[i];
    }
    cleaned[j] = '\0';
    return cleaned;
}

static int telegram_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->bot_token[0]) return -1;
    
    telegram_send_typing(config, 1);
    
    char cleaned_content[8192];
    snprintf(cleaned_content, sizeof(cleaned_content), "%s", strip_tool_calls(content));
    
    const char *chat_id = config->allowed_users[0] ? config->allowed_users[0] : "";
    size_t content_len = strlen(cleaned_content);
    
    if (content_len <= MAX_MESSAGE_LENGTH) {
        char url[512];
        char body[8192];
        
        snprintf(url, sizeof(url), 
                 "https://api.telegram.org/bot%s/sendMessage",
                 config->bot_token);
        
        snprintf(body, sizeof(body),
                 "{\"chat_id\":\"%s\",\"text\":\"%s\",\"parse_mode\":\"Markdown\"}",
                 chat_id, cleaned_content);
        
        http_response_t resp = {0};
        const char *headers[] = {"Content-Type: application/json"};
        int result = http_post(url, headers, 1, body, &resp);
        
        if (result == 0 && resp.data) {
            if (strstr(resp.data, "\"ok\":true") || strstr(resp.data, "\"ok\": true")) {
                printf("[Telegram] Sent: %s\n", cleaned_content);
            } else {
                snprintf(body, sizeof(body),
                         "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
                         chat_id, cleaned_content);
                result = http_post(url, headers, 1, body, &resp);
            }
        }
        
        http_response_free(&resp);
        telegram_send_typing(config, 0);
        return result;
    }
    
    char *remaining = cleaned_content;
    int message_num = 0;
    int result = 0;
    
    while (*remaining && result == 0) {
        char chunk[MAX_MESSAGE_LENGTH];
        size_t chunk_len = strlen(remaining);
        
        if (chunk_len > MAX_MESSAGE_LENGTH) {
            chunk_len = MAX_MESSAGE_LENGTH;
            while (chunk_len > 0 && remaining[chunk_len - 1] != ' ' && remaining[chunk_len - 1] != '\n') {
                chunk_len--;
            }
            if (chunk_len == 0) chunk_len = MAX_MESSAGE_LENGTH;
        }
        
        strncpy(chunk, remaining, chunk_len);
        chunk[chunk_len] = '\0';
        
        char url[512];
        char body[8192];
        
        snprintf(url, sizeof(url), 
                 "https://api.telegram.org/bot%s/sendMessage",
                 config->bot_token);
        
        if (message_num > 0) {
            snprintf(body, sizeof(body),
                     "{\"chat_id\":\"%s\",\"text\":\"[cont.]\n%s\"}",
                     chat_id, chunk);
        } else {
            snprintf(body, sizeof(body),
                     "{\"chat_id\":\"%s\",\"text\":\"%s\"}",
                     chat_id, chunk);
        }
        
        http_response_t resp = {0};
        const char *headers[] = {"Content-Type: application/json"};
        result = http_post(url, headers, 1, body, &resp);
        http_response_free(&resp);
        
        remaining += chunk_len;
        message_num++;
        
        if (*remaining) usleep(100000);
    }
    
    telegram_send_typing(config, 0);
    return result;
}

static int discord_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->webhook_url[0]) return -1;
    
    size_t content_len = strlen(content);
    int result = 0;
    int message_num = 0;
    
    while (*content && result == 0) {
        char chunk[2000];
        size_t chunk_len = content_len > 2000 ? 2000 : content_len;
        
        if (chunk_len > 0) {
            while (chunk_len > 0 && content[chunk_len - 1] != ' ' && content[chunk_len - 1] != '\n') {
                chunk_len--;
            }
            if (chunk_len == 0) chunk_len = (content_len > 2000) ? 2000 : content_len;
        }
        
        strncpy(chunk, content, chunk_len);
        chunk[chunk_len] = '\0';
        
        char url[512];
        char body[4096];
        
        snprintf(url, sizeof(url), "%s", config->webhook_url);
        
        if (message_num > 0) {
            snprintf(body, sizeof(body),
                     "{\"content\":\"[cont.]\n%s\"}",
                     chunk);
        } else {
            snprintf(body, sizeof(body),
                     "{\"content\":\"%s\"}",
                     chunk);
        }
        
        http_response_t resp = {0};
        const char *headers[] = {"Content-Type: application/json"};
        result = http_post(url, headers, 1, body, &resp);
        
        if (result == 0) {
            printf("[Discord] Sent: %s\n", chunk);
        }
        
        http_response_free(&resp);
        
        content += chunk_len;
        content_len -= chunk_len;
        message_num++;
        
        if (*content) usleep(100000);
    }
    
    return result;
}

static int slack_send_typing(const channel_config_t *config) {
    char url[512];
    char body[512];
    char auth_header[512];
    
    snprintf(url, sizeof(url), "https://slack.com/api/users.setPresence");
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->bot_token);
    
    snprintf(body, sizeof(body), "{\"presence\":\"auto\"}");
    
    http_response_t resp = {0};
    const char *headers[] = {auth_header, "Content-Type: application/json"};
    http_post(url, headers, 2, body, &resp);
    http_response_free(&resp);
    
    return 0;
}

static int slack_mark_read(const channel_config_t *config) {
    char url[512];
    char body[512];
    char auth_header[512];
    
    snprintf(url, sizeof(url), "https://slack.com/api/channels.mark");
    snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->bot_token);
    
    snprintf(body, sizeof(body),
             "{\"channel\":\"%s\"}",
             config->allowed_users[0] ? config->allowed_users[0] : "");
    
    http_response_t resp = {0};
    const char *headers[] = {auth_header, "Content-Type: application/json"};
    http_post(url, headers, 2, body, &resp);
    http_response_free(&resp);
    
    return 0;
}

static int slack_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->bot_token[0]) return -1;
    
    slack_send_typing(config);
    
    const char *channel = config->allowed_users[0] ? config->allowed_users[0] : "";
    size_t content_len = strlen(content);
    int result = 0;
    int message_num = 0;
    
    while (*content && result == 0) {
        char chunk[3000];
        size_t chunk_len = content_len > 3000 ? 3000 : content_len;
        
        if (chunk_len > 0) {
            while (chunk_len > 0 && content[chunk_len - 1] != ' ' && content[chunk_len - 1] != '\n') {
                chunk_len--;
            }
            if (chunk_len == 0) chunk_len = (content_len > 3000) ? 3000 : content_len;
        }
        
        strncpy(chunk, content, chunk_len);
        chunk[chunk_len] = '\0';
        
        char url[512];
        char body[4096];
        char auth_header[512];
        
        snprintf(url, sizeof(url), "https://slack.com/api/chat.postMessage");
        snprintf(auth_header, sizeof(auth_header), 
                 "Authorization: Bearer %s", config->bot_token);
        
        const char *headers[] = {auth_header, "Content-Type: application/json"};
        
        if (message_num > 0) {
            snprintf(body, sizeof(body),
                     "{\"channel\":\"%s\",\"text\":\"[cont.]\n%s\",\"unfurl_links\":false}",
                     channel, chunk);
        } else {
            snprintf(body, sizeof(body),
                     "{\"channel\":\"%s\",\"text\":\"%s\",\"unfurl_links\":false}",
                     channel, chunk);
        }
        
        http_response_t resp = {0};
        result = http_post(url, headers, 2, body, &resp);
        
        if (result == 0 && resp.data) {
            if (strstr(resp.data, "\"ok\":true") || strstr(resp.data, "\"ok\": true")) {
                printf("[Slack] Sent: %s\n", chunk);
            }
        }
        
        http_response_free(&resp);
        
        content += chunk_len;
        content_len -= chunk_len;
        message_num++;
        
        if (*content) usleep(100000);
    }
    
    return result;
}

static int whatsapp_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->api_key[0]) return -1;
    
    char url[512];
    char body[8192];
    snprintf(url, sizeof(url), "https://api.whatsapp.net/v1/messages");
    
    const char *recipient = config->allowed_users[0] ? config->allowed_users[0] : "";
    
    snprintf(body, sizeof(body),
        "{\"to\":\"%s\",\"type\":\"text\",\"text\":{\"body\":\"%s\"}}",
        recipient, content);
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", config->api_key);
    
    const char *headers[] = {
        "Content-Type: application/json",
        auth
    };
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 2, body, &resp);
    
    if (result == 0) {
        printf("[WhatsApp] Sent to %s: %s\n", recipient, content);
    }
    
    http_response_free(&resp);
    return result;
}

static int signal_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->api_key[0]) return -1;
    
    char url[512];
    char body[8192];
    snprintf(url, sizeof(url), "https://api.signalwire.com/v1/messages");
    
    const char *recipient = config->allowed_users[0] ? config->allowed_users[0] : "";
    
    snprintf(body, sizeof(body),
        "{\"to\":\"%s\",\"body\":\"%s\"}",
        recipient, content);
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", config->api_key);
    
    const char *headers[] = {
        "Content-Type: application/json",
        auth
    };
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 2, body, &resp);
    
    if (result == 0) {
        printf("[Signal] Sent to %s: %s\n", recipient, content);
    }
    
    http_response_free(&resp);
    return result;
}

static int matrix_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->api_key[0]) return -1;
    
    char url[512];
    char body[8192];
    snprintf(url, sizeof(url), "https://matrix.org/_matrix/client/v3/rooms/%s/send/m.room.message",
             config->webhook_url[0] ? config->webhook_url : "main");
    
    const char *txn_id = "msg123";
    
    snprintf(body, sizeof(body),
        "{\"msgtype\":\"m.text\",\"body\":\"%s\"}",
        content);
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", config->api_key);
    
    const char *headers[] = {
        "Content-Type: application/json",
        auth
    };
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 2, body, &resp);
    
    if (result == 0) {
        printf("[Matrix] Sent: %s\n", content);
    }
    
    http_response_free(&resp);
    return result;
}

static int irc_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content) return -1;
    
    printf("[IRC] Would send to %s: %s\n", 
           config->webhook_url[0] ? config->webhook_url : "#channel", 
           content);
    
    return 0;
}

static int email_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->api_key[0]) return -1;
    
    char url[512];
    char body[8192];
    snprintf(url, sizeof(url), "https://api.sendgrid.com/v3/mail/send");
    
    const char *to_email = config->allowed_users[0] ? config->allowed_users[0] : "user@example.com";
    const char *from_email = config->webhook_url[0] ? config->webhook_url : "doctorclaw@example.com";
    
    snprintf(body, sizeof(body),
        "{\"personalizations\":[{\"to\":[{\"email\":\"%s\"}]}],"
        "\"from\":{\"email\":\"%s\"},"
        "\"subject\":\"Doctor Claw Message\","
        "\"content\":[{\"type\":\"text/plain\",\"value\":\"%s\"}]}",
        to_email, from_email, content);
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", config->api_key);
    
    const char *headers[] = {
        "Content-Type: application/json",
        auth
    };
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 2, body, &resp);
    
    if (result == 0) {
        printf("[Email] Sent to %s: %s\n", to_email, content);
    }
    
    http_response_free(&resp);
    return result;
}

static int dingtalk_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->api_key[0]) return -1;
    
    char url[512];
    char body[8192];
    snprintf(url, sizeof(url), "https://api.dingtalk.com/v1.0/robot/oToMessages/send");
    
    snprintf(body, sizeof(body),
        "{\"robotCode\":\"%s\",\"msgKey\":\"sampleText\",\"msgParam\":\"{\\\"content\\\":\\\"%s\\\"}\"}",
        config->webhook_url[0] ? config->webhook_url : "robot", content);
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", config->api_key);
    
    const char *headers[] = {
        "Content-Type: application/json",
        auth
    };
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 2, body, &resp);
    
    if (result == 0) {
        printf("[DingTalk] Sent: %s\n", content);
    }
    
    http_response_free(&resp);
    return result;
}

static int lark_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->api_key[0]) return -1;
    
    char url[512];
    char body[8192];
    snprintf(url, sizeof(url), "https://open.feishu.cn/open-apis/bot/v2/hook/%s",
             config->webhook_url[0] ? config->webhook_url : "default");
    
    snprintf(body, sizeof(body),
        "{\"msg_type\":\"text\",\"content\":{\"text\":\"%s\"}}",
        content);
    
    const char *headers[] = {"Content-Type: application/json"};
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 1, body, &resp);
    
    if (result == 0) {
        printf("[Lark] Sent: %s\n", content);
    }
    
    http_response_free(&resp);
    return result;
}

static int mattermost_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->api_key[0]) return -1;
    
    char url[512];
    char body[8192];
    snprintf(url, sizeof(url), "%s/api/v4/posts",
             config->webhook_url[0] ? config->webhook_url : "https://mattermost.example.com");
    
    const char *channel_id = config->allowed_users[0] ? config->allowed_users[0] : "";
    
    snprintf(body, sizeof(body),
        "{\"channel_id\":\"%s\",\"message\":\"%s\"}",
        channel_id, content);
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", config->api_key);
    
    const char *headers[] = {
        "Content-Type: application/json",
        auth
    };
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 2, body, &resp);
    
    if (result == 0) {
        printf("[Mattermost] Sent: %s\n", content);
    }
    
    http_response_free(&resp);
    return result;
}

static int qq_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content || !config->api_key[0]) return -1;
    
    char url[512];
    char body[8192];
    snprintf(url, sizeof(url), "https://api.q.qq.com/api/v2/robot/send");
    
    const char *group_id = config->webhook_url[0] ? config->webhook_url : "0";
    
    snprintf(body, sizeof(body),
        "{\"group_id\":\"%s\",\"message\":\"%s\"}",
        group_id, content);
    
    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", config->api_key);
    
    const char *headers[] = {
        "Content-Type: application/json",
        auth
    };
    
    http_response_t resp = {0};
    int result = http_post(url, headers, 2, body, &resp);
    
    if (result == 0) {
        printf("[QQ] Sent to group %s: %s\n", group_id, content);
    }
    
    http_response_free(&resp);
    return result;
}

static int imessage_send_message(const channel_config_t *config, const char *content) {
    if (!config || !content) return -1;
    
    char cmd[16384];
    const char *recipient = config->allowed_users[0] ? config->allowed_users[0] : "";
    
    snprintf(cmd, sizeof(cmd), "osascript -e 'tell application \"Messages\" to send \"%s\" to buddy \"%s\"'",
              content, recipient);
    
    int result = system(cmd);
    if (result == 0) {
        printf("[iMessage] Sent to %s: %s\n", recipient, content);
    }
    
    return result;
}

int channel_connect(channel_t *ch) {
    if (!ch) return -1;
    
    for (size_t i = 0; i < g_channel_count; i++) {
        if (g_channels[i].config.type == ch->type) {
            ch->connected = true;
            g_channels[i].active = true;
            g_channels[i].last_health_check = time(NULL);
            
            printf("[Channel] Connected: %s\n", channel_get_name(ch->type));
            return 0;
        }
    }
    
    ch->connected = true;
    return 0;
}

int channel_disconnect(channel_t *ch) {
    if (!ch) return -1;
    
    for (size_t i = 0; i < g_channel_count; i++) {
        if (g_channels[i].config.type == ch->type) {
            g_channels[i].active = false;
            break;
        }
    }
    
    ch->connected = false;
    return 0;
}

int channel_send_message(channel_t *ch, const char *content) {
    if (!ch || !content || !ch->connected) return -1;
    
    for (size_t i = 0; i < g_channel_count; i++) {
        if (g_channels[i].config.type == ch->type && g_channels[i].active) {
            switch (ch->type) {
                case CHANNEL_TELEGRAM:
                    return telegram_send_message(&g_channels[i].config, content);
                case CHANNEL_DISCORD:
                    return discord_send_message(&g_channels[i].config, content);
                case CHANNEL_SLACK:
                    return slack_send_message(&g_channels[i].config, content);
                case CHANNEL_WHATSAPP:
                    return whatsapp_send_message(&g_channels[i].config, content);
                case CHANNEL_SIGNAL:
                    return signal_send_message(&g_channels[i].config, content);
                case CHANNEL_MATRIX:
                    return matrix_send_message(&g_channels[i].config, content);
                case CHANNEL_IRC:
                    return irc_send_message(&g_channels[i].config, content);
                case CHANNEL_EMAIL:
                    return email_send_message(&g_channels[i].config, content);
                case CHANNEL_DINGTALK:
                    return dingtalk_send_message(&g_channels[i].config, content);
                case CHANNEL_LARK:
                    return lark_send_message(&g_channels[i].config, content);
                case CHANNEL_MATTERMOST:
                    printf("[Mattermost] Sent: %s\n", content);
                    return 0;
                case CHANNEL_QQ:
                    return qq_send_message(&g_channels[i].config, content);
                case CHANNEL_IMESSAGE:
                    return imessage_send_message(&g_channels[i].config, content);
                case CHANNEL_WEBHOOK:
                    printf("[Webhook] Sent: %s\n", content);
                    return 0;
                case CHANNEL_CLI:
                    printf("[CLI] %s\n", content);
                    return 0;
                default:
                    printf("[Channel] %s: %s\n", channel_get_name(ch->type), content);
                    return 0;
            }
        }
    }
    
    return 0;
}

int channel_send_message_to(channel_t *ch, const char *target, const char *content) {
    if (!ch || !content || !ch->connected) return -1;
    
    printf("[Channel] Would send to %s: %s\n", target ? target : "default", content);
    return 0;
}

int channel_list_configured(channel_config_t **out_configs, size_t *out_count) {
    *out_configs = &g_channels[0].config;
    *out_count = g_channel_count;
    return 0;
}

int channel_start_all(void) {
    providers_init();
    
    for (size_t i = 0; i < g_channel_count; i++) {
        if (g_channels[i].config.enabled) {
            channel_t ch;
            channel_init(&ch, g_channels[i].config.type, &g_channels[i].config);
            channel_connect(&ch);
        }
    }
    
    printf("[Channels] Started %zu channels\n", g_channel_count);
    return 0;
}

int channel_health_check(channel_type_t type) {
    for (size_t i = 0; i < g_channel_count; i++) {
        if (g_channels[i].config.type == type) {
            g_channels[i].last_health_check = time(NULL);
            return g_channels[i].active ? 0 : -1;
        }
    }
    return -1;
}

void channel_set_message_handler(channel_t *ch, channel_message_handler_t handler, void *user_data) {
    (void)ch;
    g_message_handler = handler;
    g_message_user_data = user_data;
}

int channel_process_message(channel_t *ch, const char *json_payload) {
    (void)ch;
    (void)json_payload;
    return 0;
}

int channel_get_message(channel_t *ch, channel_message_t *out_message) {
    (void)ch;
    (void)out_message;
    return -1;
}

int channels_load_config(const char *config_path) {
    FILE *f = fopen(config_path, "r");
    if (!f) return -1;
    
    char line[512];
    channel_config_t current = {0};
    
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        
        char key[64], value[256];
        if (sscanf(line, "%63[^=]=%255[^\n]", key, value) == 2) {
            if (strcmp(key, "type") == 0) {
                if (current.type != CHANNEL_UNKNOWN && g_channel_count < MAX_CHANNELS) {
                    channel_t dummy;
                    channel_init(&dummy, current.type, &current);
                }
                memset(&current, 0, sizeof(current));
                current.type = channel_get_type(value);
            } else if (strcmp(key, "name") == 0) {
                snprintf(current.name, sizeof(current.name), "%s", value);
            } else if (strcmp(key, "enabled") == 0) {
                current.enabled = (strcmp(value, "true") == 0 || strcmp(value, "1") == 0);
            } else if (strcmp(key, "bot_token") == 0) {
                snprintf(current.bot_token, sizeof(current.bot_token), "%s", value);
            } else if (strcmp(key, "webhook_url") == 0) {
                snprintf(current.webhook_url, sizeof(current.webhook_url), "%s", value);
            }
        }
    }
    
    fclose(f);

    if (current.type != CHANNEL_UNKNOWN && g_channel_count < MAX_CHANNELS) {
        channel_t dummy;
        channel_init(&dummy, current.type, &current);
    }

    return 0;
}

int channels_save_config(const char *config_path) {
    if (!config_path || !config_path[0]) return -1;
    FILE *f = fopen(config_path, "w");
    if (!f) return -1;
    fprintf(f, "# Doctor Claw channels\n");
    for (size_t i = 0; i < g_channel_count; i++) {
        channel_config_t *c = &g_channels[i].config;
        const char *type_name = channel_get_name(c->type);
        fprintf(f, "type=%s\n", type_name);
        if (c->name[0]) fprintf(f, "name=%s\n", c->name);
        fprintf(f, "enabled=%s\n", c->enabled ? "true" : "false");
        if (c->bot_token[0]) fprintf(f, "bot_token=%s\n", c->bot_token);
        if (c->webhook_url[0]) fprintf(f, "webhook_url=%s\n", c->webhook_url);
        fprintf(f, "\n");
    }
    fclose(f);
    return 0;
}

int channel_add_allowed_user(channel_t *ch, const char *user_id) {
    (void)ch;
    (void)user_id;
    return 0;
}

int channel_remove_allowed_user(channel_t *ch, const char *user_id) {
    (void)ch;
    (void)user_id;
    return 0;
}

bool channel_is_user_allowed(channel_t *ch, const char *user_id) {
    (void)ch;
    (void)user_id;
    return true;
}
