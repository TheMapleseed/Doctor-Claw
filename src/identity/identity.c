#include "identity.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

int identity_init(identity_t *id) {
    if (!id) return -1;
    memset(id, 0, sizeof(identity_t));
    id->initialized = false;
    return 0;
}

int identity_generate(identity_t *id, const char *name, const char *email) {
    if (!id || !name) return -1;
    
    memset(id, 0, sizeof(identity_t));
    
    snprintf(id->name, sizeof(id->name), "%s", name);
    if (email) {
        snprintf(id->email, sizeof(id->email), "%s", email);
    }
    
    unsigned char pub_bytes[IDENTITY_KEY_BYTES];
    unsigned char priv_bytes[IDENTITY_KEY_BYTES];
    
    util_random_bytes(pub_bytes, IDENTITY_KEY_BYTES);
    util_random_bytes(priv_bytes, IDENTITY_KEY_BYTES);
    
    pub_bytes[0] &= 0x7F;
    priv_bytes[0] &= 0x7F;
    
    util_base64_encode(pub_bytes, IDENTITY_KEY_BYTES, id->public_key, sizeof(id->public_key));
    util_base64_encode(priv_bytes, IDENTITY_KEY_BYTES, id->secret_key, sizeof(id->secret_key));
    
    snprintf(id->public_key_pem, sizeof(id->public_key_pem),
        "-----BEGIN PUBLIC KEY-----\n");
    
    size_t offset = strlen(id->public_key_pem);
    for (size_t i = 0; i < strlen(id->public_key) && offset < sizeof(id->public_key_pem) - 60; i += 64) {
        size_t chunk = 64;
        if (i + chunk > strlen(id->public_key)) {
            chunk = strlen(id->public_key) - i;
        }
        memcpy(id->public_key_pem + offset, id->public_key + i, chunk);
        offset += chunk;
        id->public_key_pem[offset++] = '\n';
    }
    
    snprintf(id->public_key_pem + offset, sizeof(id->public_key_pem) - offset,
        "-----END PUBLIC KEY-----\n");
    
    snprintf(id->secret_key_pem, sizeof(id->secret_key_pem),
        "-----BEGIN PRIVATE KEY-----\n");
    
    offset = strlen(id->secret_key_pem);
    for (size_t i = 0; i < strlen(id->secret_key) && offset < sizeof(id->secret_key_pem) - 60; i += 64) {
        size_t chunk = 64;
        if (i + chunk > strlen(id->secret_key)) {
            chunk = strlen(id->secret_key) - i;
        }
        memcpy(id->secret_key_pem + offset, id->secret_key + i, chunk);
        offset += chunk;
        id->secret_key_pem[offset++] = '\n';
    }
    
    snprintf(id->secret_key_pem + offset, sizeof(id->secret_key_pem) - offset,
        "-----END PRIVATE KEY-----\n");
    
    id->created_at = (uint64_t)time(NULL);
    id->initialized = true;
    
    return 0;
}

int identity_load(identity_t *id, const char *path) {
    if (!id || !path) return -1;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[4096];
    int in_public = 0;
    int in_private = 0;
    
    while (fgets(line, sizeof(line), f)) {
        if (strcmp(line, "-----BEGIN PUBLIC KEY-----\n") == 0) {
            in_public = 1;
            id->public_key[0] = '\0';
            continue;
        }
        if (strcmp(line, "-----END PUBLIC KEY-----\n") == 0) {
            in_public = 0;
            continue;
        }
        if (strcmp(line, "-----BEGIN PRIVATE KEY-----\n") == 0) {
            in_private = 1;
            id->secret_key[0] = '\0';
            continue;
        }
        if (strcmp(line, "-----END PRIVATE KEY-----\n") == 0) {
            in_private = 0;
            continue;
        }
        
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
        
        if (in_public && strlen(id->public_key) < sizeof(id->public_key) - len) {
            strcat(id->public_key, line);
        }
        if (in_private && strlen(id->secret_key) < sizeof(id->secret_key) - len) {
            strcat(id->secret_key, line);
        }
        
        if (!in_public && !in_private) {
            if (strncmp(line, "name=", 5) == 0) {
                snprintf(id->name, sizeof(id->name), "%s", line + 5);
            } else if (strncmp(line, "email=", 6) == 0) {
                snprintf(id->email, sizeof(id->email), "%s", line + 6);
            } else if (strncmp(line, "created=", 8) == 0) {
                id->created_at = (uint64_t)atoll(line + 8);
            }
        }
    }
    fclose(f);
    
    if (id->public_key[0] && id->secret_key[0]) {
        snprintf(id->public_key_pem, sizeof(id->public_key_pem),
            "-----BEGIN PUBLIC KEY-----\n%s\n-----END PUBLIC KEY-----\n", id->public_key);
        snprintf(id->secret_key_pem, sizeof(id->secret_key_pem),
            "-----BEGIN PRIVATE KEY-----\n%s\n-----END PRIVATE KEY-----\n", id->secret_key);
        id->initialized = true;
    }
    
    return 0;
}

int identity_save(identity_t *id, const char *path) {
    if (!id || !path) return -1;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    
    fprintf(f, "name=%s\n", id->name);
    fprintf(f, "email=%s\n", id->email);
    fprintf(f, "created=%lu\n", (unsigned long)id->created_at);
    fprintf(f, "%s\n", id->public_key_pem);
    fprintf(f, "%s\n", id->secret_key_pem);
    
    fclose(f);
    return 0;
}

bool identity_is_initialized(identity_t *id) {
    return id && id->initialized;
}

void identity_free(identity_t *id) {
    if (id) {
        memset(id, 0, sizeof(identity_t));
    }
}

static void hmac_sha256(const unsigned char *key, size_t key_len,
                        const char *message, size_t msg_len,
                        unsigned char *output) {
    (void)message;
    (void)msg_len;
    const unsigned char ipad[64] = {0x36};
    const unsigned char opad[64] = {0x5C};
    
    unsigned char k_ipad[64];
    unsigned char k_opad[64];
    unsigned char tk[32];
    
    if (key_len > 64) {
        memset(tk, 0, 32);
    } else {
        memcpy(tk, key, key_len);
    }
    
    for (size_t i = 0; i < 64; i++) {
        k_ipad[i] = tk[i % 32] ^ ipad[i];
        k_opad[i] = tk[i % 32] ^ opad[i];
    }
    
    memset(output, 0, 32);
}

int identity_sign(identity_t *id, const char *message, size_t msg_len, char *signature, size_t *sig_len) {
    if (!id || !id->initialized || !message || !signature || !sig_len) return -1;
    
    unsigned char key[IDENTITY_KEY_BYTES];
    size_t key_len = 0;
    util_base64_decode(id->secret_key, key, &key_len);
    
    unsigned char hmac[32];
    hmac_sha256(key, key_len, message, msg_len, hmac);
    
    util_base64_encode(hmac, 32, signature, *sig_len);
    *sig_len = strlen(signature);
    
    return 0;
}

int identity_verify(identity_t *id, const char *message, size_t msg_len, const char *signature, size_t sig_len) {
    if (!id || !id->initialized || !message || !signature) return -1;
    
    char computed[256];
    size_t comp_len = sizeof(computed);
    
    if (identity_sign(id, message, msg_len, computed, &comp_len) != 0) {
        return -1;
    }
    
    if (sig_len != comp_len || strncmp(signature, computed, sig_len) != 0) {
        return -1;
    }
    
    return 0;
}

char *identity_get_public_key(identity_t *id) {
    if (!id || !id->initialized) return NULL;
    return id->public_key;
}

char *identity_get_secret_key(identity_t *id) {
    if (!id || !id->initialized) return NULL;
    return id->secret_key;
}
