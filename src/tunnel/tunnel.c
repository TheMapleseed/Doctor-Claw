#include "tunnel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <signal.h>

void tunnel_config_init(tunnel_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(tunnel_config_t));
    cfg->type = TUNNEL_TYPE_NONE;
    cfg->connected = false;
    cfg->local_port = 8080;
    cfg->remote_port = 80;
}

void tunnel_config_free(tunnel_config_t *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(tunnel_config_t));
}

int tunnel_init(tunnel_t *t) {
    if (!t) return -1;
    memset(t, 0, sizeof(tunnel_t));
    t->connected = false;
    return 0;
}

int tunnel_connect(tunnel_t *t, const char *host, int port) {
    if (!t || !host) return -1;
    snprintf(t->local_addr, sizeof(t->local_addr), "localhost:%d", port);
    snprintf(t->remote_addr, sizeof(t->remote_addr), "%s:%d", host, port);
    t->connected = true;
    return 0;
}

int tunnel_disconnect(tunnel_t *t) {
    if (!t) return -1;
    t->connected = false;
    t->local_addr[0] = '\0';
    t->remote_addr[0] = '\0';
    return 0;
}

bool tunnel_is_connected(tunnel_t *t) {
    return t && t->connected;
}

int tunnel_ngrok_start(tunnel_config_t *cfg, int port) {
    if (!cfg) return -1;
    
    char cmd[512];
    if (strlen(cfg->auth_token) > 0) {
        snprintf(cmd, sizeof(cmd), "ngrok http %d --authtoken %s 2>/dev/null &", 
                 port, cfg->auth_token);
    } else {
        snprintf(cmd, sizeof(cmd), "ngrok http %d 2>/dev/null &", port);
    }
    
    int ret = system(cmd);
    if (ret == 0) {
        cfg->connected = true;
        cfg->local_port = port;
        cfg->pid = getpid();
        printf("[Tunnel] ngrok started on port %d\n", port);
    }
    
    return ret;
}

int tunnel_ngrok_stop(tunnel_config_t *cfg) {
    if (!cfg) return -1;
    
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pkill -f 'ngrok http' 2>/dev/null");
    system(cmd);
    
    cfg->connected = false;
    printf("[Tunnel] ngrok stopped\n");
    return 0;
}

char* tunnel_ngrok_get_url(void) {
    static char url[MAX_TUNNEL_URL] = {0};
    
    FILE *fp = popen("curl -s localhost:4040/api/tunnels 2>/dev/null | grep -o 'https://[^\\\"]*' | head -1", "r");
    if (fp) {
        if (fgets(url, sizeof(url), fp)) {
            url[strcspn(url, "\n")] = 0;
        }
        pclose(fp);
    }
    
    if (url[0] == '\0') {
        snprintf(url, sizeof(url), "https://your-ngrok-url.ngrok.io");
    }
    
    return url;
}

int tunnel_tailscale_start(tunnel_config_t *cfg) {
    if (!cfg) return -1;
    
    char cmd[512];
    if (cfg->auth_token[0]) {
        snprintf(cmd, sizeof(cmd), "tailscale up --authkey=%s --ssh --exit-node 2>/dev/null &", cfg->auth_token);
    } else {
        snprintf(cmd, sizeof(cmd), "tailscale up --ssh --exit-node 2>/dev/null &");
    }
    
    int ret = system(cmd);
    if (ret == 0 || ret == 256) {
        cfg->connected = true;
        cfg->pid = getpid();
        printf("[Tunnel] Tailscale started\n");
    }
    
    return ret;
}

int tunnel_tailscale_stop(tunnel_config_t *cfg) {
    if (!cfg) return -1;
    
    system("tailscale down 2>/dev/null");
    
    cfg->connected = false;
    printf("[Tunnel] Tailscale stopped\n");
    return 0;
}

char* tunnel_tailscale_get_url(void) {
    static char url[MAX_TUNNEL_URL] = {0};
    
    FILE *fp = popen("tailscale ip -4 2>/dev/null", "r");
    if (fp) {
        if (fgets(url, sizeof(url), fp)) {
            url[strcspn(url, "\n")] = 0;
        }
        pclose(fp);
    }
    
    if (url[0] == '\0') {
        snprintf(url, sizeof(url), "100.x.x.x");
    }
    
    return url;
}

int tunnel_cloudflare_start(tunnel_config_t *cfg, int port) {
    if (!cfg) return -1;
    
    char cmd[512];
    if (strlen(cfg->auth_token) > 0) {
        snprintf(cmd, sizeof(cmd), "cloudflared tunnel --url http://localhost:%d token=%s 2>/dev/null &", 
                 port, cfg->auth_token);
    } else {
        snprintf(cmd, sizeof(cmd), "cloudflared tunnel --url http://localhost:%d 2>/dev/null &", port);
    }
    
    int ret = system(cmd);
    if (ret == 0) {
        cfg->connected = true;
        cfg->local_port = port;
        cfg->pid = getpid();
        printf("[Tunnel] Cloudflare Tunnel started on port %d\n", port);
    }
    
    return ret;
}

int tunnel_cloudflare_stop(tunnel_config_t *cfg) {
    if (!cfg) return -1;
    
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "pkill -f 'cloudflared tunnel' 2>/dev/null");
    system(cmd);
    
    cfg->connected = false;
    printf("[Tunnel] Cloudflare Tunnel stopped\n");
    return 0;
}

char* tunnel_cloudflare_get_url(void) {
    static char url[MAX_TUNNEL_URL] = {0};
    
    FILE *fp = popen("curl -s localhost:5050/tunnel 2>/dev/null | grep -o 'https://[^\\\"]*' | head -1", "r");
    if (fp) {
        if (fgets(url, sizeof(url), fp)) {
            url[strcspn(url, "\n")] = 0;
        }
        pclose(fp);
    }
    
    if (url[0] == '\0') {
        snprintf(url, sizeof(url), "https://your-tunnel.cfcd.io");
    }
    
    return url;
}

int tunnel_custom_start(tunnel_config_t *cfg, const char *command) {
    if (!cfg || !command) return -1;
    
    int ret = system(command);
    if (ret == 0) {
        cfg->connected = true;
        cfg->pid = getpid();
        printf("[Tunnel] Custom tunnel started: %s\n", command);
    }
    
    return ret;
}

int tunnel_custom_stop(tunnel_config_t *cfg) {
    if (!cfg) return -1;
    
    if (cfg->pid > 0) {
        kill(cfg->pid, SIGTERM);
    }
    
    cfg->connected = false;
    printf("[Tunnel] Custom tunnel stopped\n");
    return 0;
}

int tunnel_none_start(tunnel_config_t *cfg) {
    if (!cfg) return -1;
    cfg->connected = false;
    printf("[Tunnel] No tunnel (local only)\n");
    return 0;
}

int tunnel_none_stop(tunnel_config_t *cfg) {
    if (!cfg) return -1;
    cfg->connected = false;
    return 0;
}
