#ifndef DOCTORCLAW_LLAMA_H
#define DOCTORCLAW_LLAMA_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_MODEL_PATH 512
#define MAX_CONTEXT_SIZE 8192
#define MAX_SEED 4294967295

typedef int llama_token;

typedef struct {
    char model_path[MAX_MODEL_PATH];
    size_t n_ctx;
    size_t n_threads;
    size_t n_gpu_layers;
    float temperature;
    int64_t seed;
    bool use_mmap;
    bool use_mlock;
    bool verbose;
} llama_config_t;

typedef struct {
    void *ctx;
    bool loaded;
    char model_path[MAX_MODEL_PATH];
} llama_model_t;

typedef struct {
    char text[8192];
    size_t tokens_generated;
    bool done;
} llama_response_t;

int llama_init(llama_model_t *model, const llama_config_t *config);
int llama_load_model(llama_model_t *model, const char *model_path);
int llama_unload_model(llama_model_t *model);
int llama_chat_completion(llama_model_t *model, const char *prompt, llama_response_t *response);
int llama_chat_completion_with_config(
    llama_model_t *model,
    const char *prompt,
    float temperature,
    int64_t seed,
    size_t max_tokens,
    llama_response_t *response
);
void llama_free(llama_model_t *model);

bool llama_is_loaded(const llama_model_t *model);
const char* llama_get_model_name(const llama_model_t *model);

#endif
