#include "llama.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>

typedef int llama_token;

typedef struct {
    void *lib_handle;
    void *(*llama_init_from_file)(const char *path, const void *params);
    void (*llama_free_ctx)(void *ctx);
    int (*llama_eval)(void *ctx, const llama_token *tokens, int n_tokens, int n_past, int n_threads);
    llama_token (*llama_token_eos)(void);
    const char *(*llama_token_to_str)(void *ctx, llama_token token);
    int (*llama_n_vocab)(const void *ctx);
    int (*llama_get_kv_cache_token_count)(const void *ctx);
    void (*llama_reset_kv_cache)(void *ctx);
    int (*llama_tokenize)(void *ctx, const char *text, llama_token *tokens, int n_max_tokens, bool add_special, bool parse_special);
    const char *(*llama_model_name)(const void *model);
    bool loaded;
} llama_backend_t;

static llama_backend_t g_llama = {0};

int llama_init(llama_model_t *model, const llama_config_t *config) {
    (void)config;
    if (!model) return -1;
    
    memset(model, 0, sizeof(llama_model_t));
    
    g_llama.lib_handle = dlopen("libllama.dylib", RTLD_NOW);
    if (!g_llama.lib_handle) {
        g_llama.lib_handle = dlopen("libllama.so", RTLD_NOW);
    }
    
    if (!g_llama.lib_handle) {
        printf("[Llama] Warning: llama.cpp library not found.\n");
        printf("[Llama] Install: brew install llama.cpp\n");
        return -1;
    }
    
    *(void **)&g_llama.llama_init_from_file = dlsym(g_llama.lib_handle, "llama_init_from_file");
    *(void **)&g_llama.llama_free_ctx = dlsym(g_llama.lib_handle, "llama_free");
    *(void **)&g_llama.llama_eval = dlsym(g_llama.lib_handle, "llama_eval");
    *(void **)&g_llama.llama_token_eos = dlsym(g_llama.lib_handle, "llama_token_eos");
    *(void **)&g_llama.llama_token_to_str = dlsym(g_llama.lib_handle, "llama_token_to_str");
    *(void **)&g_llama.llama_n_vocab = dlsym(g_llama.lib_handle, "llama_n_vocab");
    *(void **)&g_llama.llama_get_kv_cache_token_count = dlsym(g_llama.lib_handle, "llama_get_kv_cache_token_count");
    *(void **)&g_llama.llama_reset_kv_cache = dlsym(g_llama.lib_handle, "llama_reset_kv_cache");
    *(void **)&g_llama.llama_tokenize = dlsym(g_llama.lib_handle, "llama_tokenize");
    *(void **)&g_llama.llama_model_name = dlsym(g_llama.lib_handle, "llama_model_name");
    
    if (!g_llama.llama_init_from_file || !g_llama.llama_free_ctx) {
        printf("[Llama] Error: Failed to load llama.cpp functions\n");
        dlclose(g_llama.lib_handle);
        g_llama.lib_handle = NULL;
        return -1;
    }
    
    printf("[Llama] Backend initialized\n");
    return 0;
}

int llama_load_model(llama_model_t *model, const char *model_path) {
    if (!model || !model_path) return -1;
    
    if (!g_llama.lib_handle) {
        if (llama_init(model, NULL) != 0) {
            return -1;
        }
    }
    
    model->ctx = g_llama.llama_init_from_file(model_path, NULL);
    
    if (!model->ctx) {
        printf("[Llama] Error: Failed to load model from %s\n", model_path);
        return -1;
    }
    
    snprintf(model->model_path, sizeof(model->model_path), "%s", model_path);
    model->loaded = true;
    
    const char *name = "Unknown";
    if (g_llama.llama_model_name) {
        name = g_llama.llama_model_name(model->ctx);
    }
    printf("[Llama] Loaded model: %s\n", name ? name : model_path);
    
    return 0;
}

int llama_unload_model(llama_model_t *model) {
    if (!model || !model->loaded) return -1;
    
    if (model->ctx && g_llama.llama_free_ctx) {
        g_llama.llama_free_ctx(model->ctx);
    }
    
    model->ctx = NULL;
    model->loaded = false;
    
    return 0;
}

int llama_chat_completion(llama_model_t *model, const char *prompt, llama_response_t *response) {
    return llama_chat_completion_with_config(model, prompt, 0.8, -1, 512, response);
}

int llama_chat_completion_with_config(
    llama_model_t *model,
    const char *prompt,
    float temperature,
    int64_t seed,
    size_t max_tokens,
    llama_response_t *response
) {
    (void)temperature;
    (void)seed;
    if (!model || !model->loaded || !prompt || !response) return -1;
    
    memset(response, 0, sizeof(llama_response_t));
    
    if (!g_llama.llama_tokenize || !g_llama.llama_eval || !g_llama.llama_token_eos) {
        snprintf(response->text, sizeof(response->text), 
                 "Error: llama.cpp functions not available");
        return -1;
    }
    
    const int n_tokens_max = 4096;
    llama_token tokens[n_tokens_max];
    int n_tokens = g_llama.llama_tokenize(model->ctx, prompt, tokens, n_tokens_max, true, false);
    
    if (n_tokens < 0) {
        snprintf(response->text, sizeof(response->text), "Error: Failed to tokenize prompt");
        return -1;
    }
    
    if (g_llama.llama_reset_kv_cache) {
        g_llama.llama_reset_kv_cache(model->ctx);
    }
    
    int n_past = 0;
    int eval_result = g_llama.llama_eval(model->ctx, tokens, n_tokens, n_past, 4);
    
    if (eval_result != 0) {
        snprintf(response->text, sizeof(response->text), "Error: Failed to evaluate prompt");
        return -1;
    }
    
    n_past += n_tokens;
    
    char output[8192] = {0};
    size_t output_len = 0;
    
    llama_token eos_token = g_llama.llama_token_eos();
    
    for (size_t i = 0; i < max_tokens && output_len < sizeof(output) - 32; i++) {
        int next_token = tokens[0];
        
        const char *token_str = g_llama.llama_token_to_str(model->ctx, next_token);
        if (token_str) {
            size_t len = strlen(token_str);
            if (output_len + len < sizeof(output) - 1) {
                strcat(output, token_str);
                output_len += len;
            }
        }
        
        if (next_token == eos_token) {
            break;
        }
        
        eval_result = g_llama.llama_eval(model->ctx, &next_token, 1, n_past, 4);
        
        if (eval_result != 0) {
            break;
        }
        
        n_past++;
    }
    
    snprintf(response->text, sizeof(response->text), "%s", output);
    response->tokens_generated = output_len;
    response->done = true;
    
    return 0;
}

void llama_free(llama_model_t *model) {
    if (!model) return;
    
    llama_unload_model(model);
    
    if (g_llama.lib_handle) {
        dlclose(g_llama.lib_handle);
        g_llama.lib_handle = NULL;
    }
}

bool llama_is_loaded(const llama_model_t *model) {
    return model && model->loaded;
}

const char* llama_get_model_name(const llama_model_t *model) {
    if (!model || !model->loaded) return NULL;
    return model->model_path;
}
