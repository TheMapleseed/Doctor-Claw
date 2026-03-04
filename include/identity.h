#ifndef DOCTORCLAW_IDENTITY_H
#define DOCTORCLAW_IDENTITY_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_IDENTITY_NAME 128
#define MAX_IDENTITY_EMAIL 256
#define IDENTITY_KEY_BITS 2048
#define IDENTITY_KEY_BYTES 256

typedef struct {
    char name[MAX_IDENTITY_NAME];
    char email[MAX_IDENTITY_EMAIL];
    char public_key[IDENTITY_KEY_BYTES * 2];
    char secret_key[IDENTITY_KEY_BYTES * 2];
    char public_key_pem[IDENTITY_KEY_BYTES * 4];
    char secret_key_pem[IDENTITY_KEY_BYTES * 4];
    uint64_t created_at;
    bool initialized;
} identity_t;

int identity_init(identity_t *id);
int identity_generate(identity_t *id, const char *name, const char *email);
int identity_load(identity_t *id, const char *path);
int identity_save(identity_t *id, const char *path);
bool identity_is_initialized(identity_t *id);
void identity_free(identity_t *id);

int identity_sign(identity_t *id, const char *message, size_t msg_len, char *signature, size_t *sig_len);
int identity_verify(identity_t *id, const char *message, size_t msg_len, const char *signature, size_t sig_len);
char *identity_get_public_key(identity_t *id);
char *identity_get_secret_key(identity_t *id);

#endif
