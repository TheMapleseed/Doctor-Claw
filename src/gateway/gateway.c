#include "config.h"
#include "channels.h"
#include "gateway.h"
#include "agent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdbool.h>

#define MAX_REQUEST_SIZE 65536
#define MAX_RESPONSE_SIZE 65536
#define MAX_MESSAGE_CONTENT 16384

typedef struct {
    char method[16];
    char path[256];
    char body[MAX_REQUEST_SIZE];
    size_t body_size;
    char headers[32][512];
    size_t header_count;
} http_request_t;

typedef struct {
    int status_code;
    char status_text[32];
    char body[MAX_RESPONSE_SIZE];
    size_t body_size;
    char content_type[64];
} gateway_response_t;

static const char *g_html_template = 
    "<!DOCTYPE html>\n"
    "<html><head><title>Doctor Claw</title></head>\n"
    "<body><h1>Doctor Claw Gateway</h1>\n"
    "<p>Version: %s</p>\n"
    "<p>Status: Running</p>\n"
    "<p><a href=\"/webhooks\">Webhooks</a> | <a href=\"/health\">Health</a></p>\n"
    "</body></html>\n";

static int parse_http_request(const char *raw, size_t len, http_request_t *req) {
    if (!raw || !req) return -1;
    
    memset(req, 0, sizeof(http_request_t));
    
    const char *body_start = strstr(raw, "\r\n\r\n");
    if (body_start) {
        size_t header_len = body_start - raw;
        if (header_len < len - 4) {
            req->body_size = len - header_len - 4;
            if (req->body_size >= sizeof(req->body)) {
                req->body_size = sizeof(req->body) - 1;
            }
            memcpy(req->body, body_start + 4, req->body_size);
            req->body[req->body_size] = '\0';
        }
        
        char header_part[4096];
        size_t copy_len = header_len < sizeof(header_part) - 1 ? header_len : sizeof(header_part) - 1;
        memcpy(header_part, raw, copy_len);
        header_part[copy_len] = '\0';
        
        char method[16], path[256];
        if (sscanf(header_part, "%15s %255s", method, path) == 2) {
            snprintf(req->method, sizeof(req->method), "%s", method);
            snprintf(req->path, sizeof(req->path), "%s", path);
        }
        
        const char *header_line = header_part;
        while (header_line && header_line < body_start) {
            const char *next_line = strstr(header_line, "\r\n");
            if (!next_line) break;
            
            size_t line_len = next_line - header_line;
            if (line_len < sizeof(req->headers[req->header_count]) && req->header_count < 32) {
                memcpy(req->headers[req->header_count], header_line, line_len);
                req->headers[req->header_count][line_len] = '\0';
                req->header_count++;
            }
            header_line = next_line + 2;
        }
    }
    
    return 0;
}

static const char *get_header(const http_request_t *req, const char *name) {
    for (size_t i = 0; i < req->header_count; i++) {
        size_t name_len = strlen(name);
        if (strncmp(req->headers[i], name, name_len) == 0 && 
            req->headers[i][name_len] == ':') {
            const char *value = req->headers[i] + name_len + 1;
            while (*value == ' ') value++;
            return value;
        }
    }
    return NULL;
}

static int handle_ws_request(const http_request_t *req, int client_fd) {
    const char *upgrade = get_header(req, "Upgrade");
    const char *connection = get_header(req, "Connection");
    const char *ws_key = get_header(req, "Sec-WebSocket-Key");
    const char *ws_version = get_header(req, "Sec-WebSocket-Version");
    
    if (!upgrade || !connection || !ws_key) {
        return -1;
    }
    
    if (strstr(connection, "Upgrade") == NULL || strstr(ws_key, "==") == NULL) {
        return -1;
    }
    
    printf("[Gateway] WebSocket upgrade request for %s\n", req->path);
    
    return ws_handshake(client_fd, ws_key);
}

int ws_parse_frame(const char *data, size_t len, char *out_message, size_t *out_len, bool *out_binary);

static int ws_send_to_fd(int client_fd, const char *message, size_t len, bool binary);

static void build_http_response(const gateway_response_t *resp, char *out, size_t *out_size) {
    snprintf(out, *out_size,
             "HTTP/1.1 %d %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "Access-Control-Allow-Origin: *\r\n"
             "\r\n",
             resp->status_code, resp->status_text,
             resp->content_type, resp->body_size);
    
    size_t header_len = strlen(out);
    if (header_len + resp->body_size < *out_size) {
        memcpy(out + header_len, resp->body, resp->body_size);
        *out_size = header_len + resp->body_size;
    } else {
        *out_size = header_len;
    }
}

static void json_escape(const char *in, char *out, size_t out_size) {
    size_t j = 0;
    for (; in && *in && j + 2 < out_size; in++) {
        if (*in == '"' || *in == '\\') {
            out[j++] = '\\';
            out[j++] = *in;
        } else if (*in == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (*in == '\r') {
            out[j++] = '\\';
            out[j++] = 'r';
        } else {
            out[j++] = *in;
        }
    }
    out[j] = '\0';
}

static int parse_json_prompt(const char *body, char *prompt, size_t prompt_size) {
    const char *p = body ? strstr(body, "\"prompt\"") : NULL;
    if (!p) return -1;
    p += 7;
    while (*p && *p != ':') p++;
    if (!*p) return -1;
    p++;
    while (*p && (*p == ' ' || *p == '\t')) p++;
    if (*p != '"') return -1;
    p++;
    size_t i = 0;
    while (*p && i + 1 < prompt_size) {
        if (*p == '\\' && (p[1] == '"' || p[1] == '\\')) {
            p++;
            prompt[i++] = *p++;
            continue;
        }
        if (*p == '"') break;
        prompt[i++] = *p++;
    }
    prompt[i] = '\0';
    return 0;
}

static void handle_request(const http_request_t *req, gateway_response_t *resp, config_t *config) {
    memset(resp, 0, sizeof(gateway_response_t));
    
    if (strcmp(req->path, "/") == 0 || strcmp(req->path, "/index.html") == 0) {
        resp->status_code = 200;
        snprintf(resp->status_text, sizeof(resp->status_text), "OK");
        snprintf(resp->content_type, sizeof(resp->content_type), "text/html");
        snprintf(resp->body, sizeof(resp->body), g_html_template, "0.1.0");
        resp->body_size = strlen(resp->body);
    } else if (strcmp(req->path, "/health") == 0) {
        resp->status_code = 200;
        snprintf(resp->status_text, sizeof(resp->status_text), "OK");
        snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
        snprintf(resp->body, sizeof(resp->body), "{\"status\":\"healthy\",\"version\":\"0.1.0\"}");
        resp->body_size = strlen(resp->body);
    } else if (strcmp(req->path, "/webhook") == 0 || strcmp(req->path, "/webhooks") == 0) {
        resp->status_code = 200;
        snprintf(resp->status_text, sizeof(resp->status_text), "OK");
        snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
        snprintf(resp->body, sizeof(resp->body), 
                 "{\"message\":\"Webhook endpoint ready\",\"paths\":[\"/telegram\",\"/discord\",\"/slack\"]}");
        resp->body_size = strlen(resp->body);
    } else if (strncmp(req->path, "/telegram", 9) == 0) {
        resp->status_code = 200;
        snprintf(resp->status_text, sizeof(resp->status_text), "OK");
        snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
        
        char message[MAX_MESSAGE_CONTENT] = {0};
        if (sscanf(req->body, "{\"message\":{\"text\":\"%2047[^\"]\"}}", message) == 1) {
            snprintf(resp->body, sizeof(resp->body), 
                     "{\"status\":\"received\",\"message\":\"%s\"}", message);
        } else {
            snprintf(resp->body, sizeof(resp->body), "{\"status\":\"ok\"}");
        }
        resp->body_size = strlen(resp->body);
    } else if (strcmp(req->path, "/agent/chat") == 0 && (req->method[0] == 'P' || req->method[0] == 'p')) {
        resp->status_code = 200;
        snprintf(resp->status_text, sizeof(resp->status_text), "OK");
        snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
        
        char prompt[4096] = {0};
        if (parse_json_prompt(req->body, prompt, sizeof(prompt)) != 0 || !prompt[0]) {
            snprintf(resp->body, sizeof(resp->body), "{\"error\":\"No prompt provided\"}");
            resp->body_size = strlen(resp->body);
        } else {
            bool task_focus = (strstr(req->body, "\"task_focus\":true") != NULL ||
                              strstr(req->body, "\"task_focus\": true") != NULL);
            agent_t agent = {0};
            if (config && agent_init(&agent, config) == 0) {
                char response_buf[16384] = {0};
                int chat_ret = task_focus
                    ? agent_run_task(&agent, prompt, response_buf, sizeof(response_buf))
                    : agent_chat(&agent, prompt, response_buf, sizeof(response_buf));
                agent_free(&agent);
                if (chat_ret == 0 && response_buf[0]) {
                    char escaped[32768];
                    json_escape(response_buf, escaped, sizeof(escaped));
                    snprintf(resp->body, sizeof(resp->body), "{\"response\":\"%s\"}", escaped);
                } else {
                    snprintf(resp->body, sizeof(resp->body), "{\"error\":\"%s\"}",
                             response_buf[0] ? response_buf : "Agent chat failed");
                }
            } else {
                snprintf(resp->body, sizeof(resp->body), "{\"error\":\"Agent init failed\"}");
            }
            resp->body_size = strlen(resp->body);
        }
    } else {
        resp->status_code = 404;
        snprintf(resp->status_text, sizeof(resp->status_text), "Not Found");
        snprintf(resp->content_type, sizeof(resp->content_type), "application/json");
        snprintf(resp->body, sizeof(resp->body), "{\"error\":\"Not found\"}");
        resp->body_size = strlen(resp->body);
    }
}

int gateway_run(const char *host, uint16_t port, config_t *config) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return -1;
    }
    
    int opt = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    
    if (strcmp(host, "0.0.0.0") == 0) {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        inet_pton(AF_INET, host, &addr.sin_addr);
    }
    
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sockfd);
        return -1;
    }
    
    if (listen(sockfd, 10) < 0) {
        perror("listen");
        close(sockfd);
        return -1;
    }
    
    printf("[Gateway] Listening on http://%s:%d\n", host, port);
    
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_len);
        
        if (client_fd < 0) {
            perror("accept");
            continue;
        }
        
        char buffer[MAX_REQUEST_SIZE + MAX_RESPONSE_SIZE];
        ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        
        if (n > 0) {
            buffer[n] = '\0';
            
            http_request_t req;
            parse_http_request(buffer, n, &req);
            
            const char *upgrade = get_header(&req, "Upgrade");
            if (upgrade && strcmp(upgrade, "websocket") == 0) {
                printf("[Gateway] Handling WebSocket upgrade\n");
                handle_ws_request(&req, client_fd);
                
                bool running = true;
                
                while (running) {
                    ssize_t ws_n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
                    if (ws_n <= 0) break;
                    
                    char decoded[WS_MAX_MESSAGE_SIZE];
                    size_t decoded_len = 0;
                    bool binary = false;
                    
                    ws_parse_frame(buffer, (size_t)ws_n, decoded, &decoded_len, &binary);
                    
                    if ((uint8_t)buffer[0] == 0x8) {
                        running = false;
                    }
                    
                    if (decoded_len > 0) {
                        printf("[Gateway] WS received: %s\n", decoded);
                        char response[WS_MAX_MESSAGE_SIZE];
                        snprintf(response, sizeof(response), "Echo: %s", decoded);
                        ws_send_to_fd(client_fd, response, strlen(response), false);
                    }
                }
                
                close(client_fd);
                continue;
            }
            
            gateway_response_t resp;
            handle_request(&req, &resp, config);
            
            char response_buf[MAX_REQUEST_SIZE + MAX_RESPONSE_SIZE];
            size_t resp_size = sizeof(response_buf);
            build_http_response(&resp, response_buf, &resp_size);
            
            send(client_fd, response_buf, resp_size, 0);
        }
        
        close(client_fd);
    }
    
    close(sockfd);
    return 0;
}

static const char WS_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static void sha1_hash(const char *input, size_t len, char *output) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    
    size_t msg_len = len * 8;
    size_t pad_len = (len < 56) ? (56 - len - 1) : (120 - len - 1);
    
    char msg[128];
    memcpy(msg, input, len);
    msg[len] = 0x80;
    memset(msg + len + 1, 0, pad_len);
    
    for (int i = 0; i < 8; i++) {
        msg[len + 1 + pad_len + i] = (msg_len >> (56 - i * 8)) & 0xFF;
    }
    
    for (size_t chunk = 0; chunk < len + 1 + pad_len + 8; chunk += 64) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) {
            w[i] = ((uint8_t)msg[chunk + i * 4] << 24) |
                    ((uint8_t)msg[chunk + i * 4 + 1] << 16) |
                    ((uint8_t)msg[chunk + i * 4 + 2] << 8) |
                    ((uint8_t)msg[chunk + i * 4 + 3]);
        }
        for (int i = 16; i < 80; i++) {
            uint32_t val = w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16];
            w[i] = (val << 1) | (val >> 31);
        }
        
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6;
            }
            
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d;
            d = c;
            c = ((b << 30) | (b >> 2));
            b = a;
            a = temp;
        }
        
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
    }
    
    snprintf(output, 41, "%08x%08x%08x%08x%08x", h[0], h[1], h[2], h[3], h[4]);
}

int ws_handshake(int client_fd, const char *key) {
    char combined[256];
    snprintf(combined, sizeof(combined), "%s%s", key, WS_GUID);
    
    char hash[41];
    sha1_hash(combined, strlen(combined), hash);
    
    char response[512];
    snprintf(response, sizeof(response),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n"
        "\r\n",
        hash);
    
    send(client_fd, response, strlen(response), 0);
    return 0;
}

int ws_parse_frame(const char *data, size_t len, char *out_message, size_t *out_len, bool *out_binary) {
    if (len < 2) return -1;
    
    uint8_t first_byte = (uint8_t)data[0];
    uint8_t second_byte = (uint8_t)data[1];
    uint8_t opcode = first_byte & 0x0F;
    bool masked = (second_byte & 0x80) != 0;
    uint64_t payload_len = second_byte & 0x7F;
    
    *out_binary = (opcode == 2);
    
    size_t offset = 2;
    
    if (payload_len == 126) {
        if (len < 4) return -1;
        payload_len = ((uint8_t)data[2] << 8) | (uint8_t)data[3];
        offset = 4;
    } else if (payload_len == 127) {
        if (len < 10) return -1;
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | (uint8_t)data[2 + i];
        }
        offset = 10;
    }
    
    size_t mask_len = masked ? 4 : 0;
    if (len < offset + mask_len + payload_len) return -1;
    
    char mask_key[4] = {0};
    if (masked) {
        memcpy(mask_key, data + offset, 4);
        offset += 4;
    }
    
    for (size_t i = 0; i < payload_len && i < *out_len - 1; i++) {
        uint8_t byte = (uint8_t)data[offset + i];
        if (masked) {
            byte ^= (uint8_t)mask_key[i % 4];
        }
        out_message[i] = (char)byte;
    }
    out_message[payload_len] = '\0';
    *out_len = payload_len;
    
    return (int)((first_byte >> 7) & 1);
}

static void ws_build_frame(const char *message, size_t len, bool binary, char *out, size_t *out_len) {
    out[0] = (char)(binary ? 0x82 : 0x81);
    
    if (len < 126) {
        out[1] = (char)(uint8_t)len;
        *out_len = 2;
    } else if (len < 65536) {
        out[1] = 126;
        out[2] = (char)((len >> 8) & 0xFF);
        out[3] = (char)(len & 0xFF);
        *out_len = 4;
    } else {
        out[1] = 127;
        for (int i = 7; i >= 0; i--) {
            out[2 + i] = (char)((len >> (i * 8)) & 0xFF);
        }
        *out_len = 10;
    }
    
    memcpy(out + *out_len, message, len);
    *out_len += len;
}

static int ws_send_to_fd(int client_fd, const char *message, size_t len, bool binary) {
    if (client_fd < 0 || !message) return -1;
    char frame[WS_MAX_MESSAGE_SIZE + 14];
    size_t frame_len = 0;
    ws_build_frame(message, len, binary, frame, &frame_len);
    return (send(client_fd, frame, frame_len, 0) == (ssize_t)frame_len) ? 0 : -1;
}

int ws_init(ws_server_t *ws) {
    if (!ws) return -1;
    memset(ws, 0, sizeof(ws_server_t));
    ws->client_count = 0;
    return 0;
}

int ws_add_client(ws_server_t *ws, int fd, const char *ip) {
    if (!ws || fd < 0) return -1;
    if (ws->client_count >= WS_MAX_CLIENTS) return -1;
    
    ws->clients[ws->client_count].fd = fd;
    snprintf(ws->clients[ws->client_count].ip, sizeof(ws->clients[ws->client_count].ip), "%s", ip);
    ws->clients[ws->client_count].connected_at = (uint64_t)time(NULL);
    ws->clients[ws->client_count].authenticated = false;
    ws->client_count++;
    
    if (ws->connect_handler) {
        ws->connect_handler(fd);
    }
    
    return 0;
}

int ws_remove_client(ws_server_t *ws, int fd) {
    if (!ws) return -1;
    
    for (size_t i = 0; i < ws->client_count; i++) {
        if (ws->clients[i].fd == fd) {
            if (ws->disconnect_handler) {
                ws->disconnect_handler(fd);
            }
            
            for (size_t j = i; j < ws->client_count - 1; j++) {
                ws->clients[j] = ws->clients[j + 1];
            }
            ws->client_count--;
            return 0;
        }
    }
    return -1;
}

int ws_broadcast(ws_server_t *ws, const char *message, size_t len, bool binary) {
    if (!ws || !message) return -1;
    
    char frame[WS_MAX_MESSAGE_SIZE + 14];
    size_t frame_len = 0;
    ws_build_frame(message, len, binary, frame, &frame_len);
    
    for (size_t i = 0; i < ws->client_count; i++) {
        send(ws->clients[i].fd, frame, frame_len, 0);
    }
    
    return 0;
}

int ws_send(ws_server_t *ws, int client_fd, const char *message, size_t len, bool binary) {
    if (!ws || !message) return -1;
    
    char frame[WS_MAX_MESSAGE_SIZE + 14];
    size_t frame_len = 0;
    ws_build_frame(message, len, binary, frame, &frame_len);
    
    for (size_t i = 0; i < ws->client_count; i++) {
        if (ws->clients[i].fd == client_fd) {
            return send(client_fd, frame, frame_len, 0);
        }
    }
    return -1;
}

void ws_free(ws_server_t *ws) {
    if (!ws) return;
    for (size_t i = 0; i < ws->client_count; i++) {
        if (ws->clients[i].fd > 0) {
            close(ws->clients[i].fd);
        }
    }
    memset(ws, 0, sizeof(ws_server_t));
}
