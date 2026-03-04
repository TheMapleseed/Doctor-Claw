#ifndef DOCTORCLAW_GATEWAY_H
#define DOCTORCLAW_GATEWAY_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

#define WS_MAX_CLIENTS 32
#define WS_MAX_MESSAGE_SIZE 65536

typedef enum {
    WS_OP_CONTINUATION = 0x0,
    WS_OP_TEXT = 0x1,
    WS_OP_BINARY = 0x2,
    WS_OP_CLOSE = 0x8,
    WS_OP_PING = 0x9,
    WS_OP_PONG = 0xA
} ws_opcode_t;

typedef struct {
    int fd;
    char ip[64];
    uint64_t connected_at;
    bool authenticated;
    char user_id[64];
} ws_client_t;

typedef struct {
    ws_client_t clients[WS_MAX_CLIENTS];
    size_t client_count;
    void (*message_handler)(int client_fd, const char *message, size_t len);
    void (*connect_handler)(int client_fd);
    void (*disconnect_handler)(int client_fd);
} ws_server_t;

int gateway_run(const char *host, uint16_t port, config_t *config);

int ws_init(ws_server_t *ws);
int ws_add_client(ws_server_t *ws, int fd, const char *ip);
int ws_remove_client(ws_server_t *ws, int fd);
int ws_broadcast(ws_server_t *ws, const char *message, size_t len, bool binary);
int ws_send(ws_server_t *ws, int client_fd, const char *message, size_t len, bool binary);
void ws_free(ws_server_t *ws);

int ws_handshake(int client_fd, const char *key);

#endif
