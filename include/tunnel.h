#ifndef DOCTORCLAW_TUNNEL_H
#define DOCTORCLAW_TUNNEL_H

#include "c23_check.h"
#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

#define MAX_TUNNEL_NAME 64
#define MAX_TUNNEL_URL 256

typedef enum {
    TUNNEL_TYPE_NONE,
    TUNNEL_TYPE_TAILSCALE,
    TUNNEL_TYPE_NGROK,
    TUNNEL_TYPE_CLOUDFLARE,
    TUNNEL_TYPE_CUSTOM,
    TUNNEL_TYPE_SSH
} tunnel_type_t;

typedef struct {
    tunnel_type_t type;
    char name[MAX_TUNNEL_NAME];
    char url[MAX_TUNNEL_URL];
    char auth_token[256];
    bool connected;
    int local_port;
    int remote_port;
    pid_t pid;
} tunnel_config_t;

typedef struct {
    tunnel_type_t type;
    bool connected;
    char local_addr[64];
    char remote_addr[64];
    char public_url[MAX_TUNNEL_URL];
    pid_t pid;
} tunnel_t;

int tunnel_init(tunnel_t *t);
int tunnel_connect(tunnel_t *t, const char *host, int port);
int tunnel_disconnect(tunnel_t *t);
bool tunnel_is_connected(tunnel_t *t);

int tunnel_ngrok_start(tunnel_config_t *cfg, int port);
int tunnel_ngrok_stop(tunnel_config_t *cfg);
char* tunnel_ngrok_get_url(void);

int tunnel_tailscale_start(tunnel_config_t *cfg);
int tunnel_tailscale_stop(tunnel_config_t *cfg);
char* tunnel_tailscale_get_url(void);

int tunnel_cloudflare_start(tunnel_config_t *cfg, int port);
int tunnel_cloudflare_stop(tunnel_config_t *cfg);
char* tunnel_cloudflare_get_url(void);

int tunnel_custom_start(tunnel_config_t *cfg, const char *command);
int tunnel_custom_stop(tunnel_config_t *cfg);

int tunnel_none_start(tunnel_config_t *cfg);
int tunnel_none_stop(tunnel_config_t *cfg);

void tunnel_config_init(tunnel_config_t *cfg);
void tunnel_config_free(tunnel_config_t *cfg);

#endif
