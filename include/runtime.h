#ifndef DOCTORCLAW_RUNTIME_H
#define DOCTORCLAW_RUNTIME_H

#include "c23_check.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MAX_RUNTIME_NAME 64
#define MAX_RUNTIME_ARGS 32

typedef enum {
    RUNTIME_STATE_IDLE,
    RUNTIME_STATE_RUNNING,
    RUNTIME_STATE_PAUSED,
    RUNTIME_STATE_STOPPED
} runtime_state_t;

typedef enum {
    RUNTIME_TYPE_NATIVE,
    RUNTIME_TYPE_DOCKER,
    RUNTIME_TYPE_WASM,
    RUNTIME_TYPE_WASI
} runtime_type_t;

typedef struct {
    runtime_state_t state;
    uint64_t started_at;
    uint64_t uptime_seconds;
    size_t memory_usage;
} runtime_info_t;

typedef struct {
    runtime_type_t type;
    char name[MAX_RUNTIME_NAME];
    char image[128];
    char container_id[64];
    char working_dir[256];
    char *args[MAX_RUNTIME_ARGS];
    size_t arg_count;
    bool network_enabled;
    bool privileged;
    size_t memory_limit;
    size_t cpu_limit;
} runtime_config_t;

typedef struct {
    char *data;
    size_t size;
} wasm_module_t;

typedef void* (*runtime_init_fn)(const runtime_config_t *config);
typedef int (*runtime_run_fn)(void *ctx, const char *code, char *output, size_t output_size);
typedef int (*runtime_cleanup_fn)(void *ctx);

int runtime_init(void);
int runtime_start(void);
int runtime_stop(void);
int runtime_pause(void);
int runtime_resume(void);
int runtime_get_info(runtime_info_t *info);
void runtime_shutdown(void);

int runtime_native_init(const runtime_config_t *config);
int runtime_native_run(const char *code, char *output, size_t output_size);
int runtime_native_cleanup(void);

int runtime_docker_init(const runtime_config_t *config);
int runtime_docker_run(const char *command, char *output, size_t output_size);
int runtime_docker_stop(void);
int runtime_docker_cleanup(void);

int runtime_wasm_init(const runtime_config_t *config);
int runtime_wasm_load(const wasm_module_t *module);
int runtime_wasm_run(const char *function, char *output, size_t output_size);
int runtime_wasm_cleanup(void);

int runtime_wasi_init(const char *wasm_path);
int runtime_wasi_run(const char *args[], char *output, size_t output_size);
int runtime_wasi_cleanup(void);

int runtime_docker_build(const char *dockerfile, const char *tag);
int runtime_docker_pull(const char *image);
int runtime_docker_list(char *output, size_t output_size);
int runtime_docker_remove(const char *container_id);

#endif
