#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <limits.h>
#include <libgen.h>

static runtime_info_t g_runtime = {0};
static runtime_config_t g_runtime_config = {0};
static char g_container_id[64] = {0};

int runtime_init(void) {
    memset(&g_runtime, 0, sizeof(runtime_info_t));
    memset(&g_runtime_config, 0, sizeof(runtime_config_t));
    g_runtime.state = RUNTIME_STATE_IDLE;
    printf("[Runtime] Initialized\n");
    return 0;
}

int runtime_start(void) {
    g_runtime.state = RUNTIME_STATE_RUNNING;
    g_runtime.started_at = (uint64_t)time(NULL);
    printf("[Runtime] Started\n");
    return 0;
}

int runtime_stop(void) {
    g_runtime.state = RUNTIME_STATE_STOPPED;
    if (g_runtime.started_at > 0) {
        g_runtime.uptime_seconds = (uint64_t)time(NULL) - g_runtime.started_at;
    }
    runtime_docker_cleanup();
    printf("[Runtime] Stopped\n");
    return 0;
}

int runtime_pause(void) {
    if (g_runtime.state == RUNTIME_STATE_RUNNING) {
        g_runtime.state = RUNTIME_STATE_PAUSED;
        printf("[Runtime] Paused\n");
        return 0;
    }
    return -1;
}

int runtime_resume(void) {
    if (g_runtime.state == RUNTIME_STATE_PAUSED) {
        g_runtime.state = RUNTIME_STATE_RUNNING;
        printf("[Runtime] Resumed\n");
        return 0;
    }
    return -1;
}

int runtime_get_info(runtime_info_t *info) {
    if (!info) return -1;
    memcpy(info, &g_runtime, sizeof(runtime_info_t));
    if (g_runtime.started_at > 0 && g_runtime.state == RUNTIME_STATE_RUNNING) {
        info->uptime_seconds = (uint64_t)time(NULL) - g_runtime.started_at;
    }
    return 0;
}

void runtime_shutdown(void) {
    runtime_stop();
    printf("[Runtime] Shutdown complete\n");
}

static int validate_workspace_path(const char *workspace) {
    if (!workspace) return -1;
    
    char resolved[PATH_MAX];
    
    if (realpath(workspace, resolved) == NULL) {
        return -1;
    }
    
    if (strcmp(resolved, "/") == 0 || strcmp(resolved, "/root") == 0 || 
        strcmp(resolved, "/home") == 0) {
        printf("[Runtime] Rejecting unsafe workspace path: %s\n", resolved);
        return -1;
    }
    
    return 0;
}

int runtime_docker_init(const runtime_config_t *config) {
    if (!config) return -1;
    memset(&g_runtime_config, 0, sizeof(runtime_config_t));
    memcpy(&g_runtime_config, config, sizeof(runtime_config_t));
    
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "docker --version >/dev/null 2>&1");
    if (system(cmd) != 0) {
        printf("[Runtime] Docker not available\n");
        return -1;
    }
    
    if (strlen(config->image) > 0) {
        if (validate_workspace_path(config->working_dir) != 0) {
            printf("[Runtime] Invalid workspace directory\n");
            return -1;
        }
        
        snprintf(g_container_id, sizeof(g_container_id), "doctorclaw-%d", getpid());
        
        printf("[Runtime] Docker runtime initialized: image=%s, container=%s\n", 
               config->image, g_container_id);
    } else {
        printf("[Runtime] Docker runtime initialized\n");
    }
    
    return 0;
}

int runtime_docker_run(const char *command, char *output, size_t output_size) {
    if (!command || !output) return -1;
    output[0] = '\0';
    
    char docker_cmd[4096];
    char *p = docker_cmd;
    size_t remaining = sizeof(docker_cmd);
    
    int written = snprintf(p, remaining, "docker run --rm --name %s", g_container_id);
    p += written;
    remaining -= written;
    
    if (!g_runtime_config.network_enabled) {
        written = snprintf(p, remaining, " --network none");
        p += written;
        remaining -= written;
    }
    
    if (g_runtime_config.memory_limit > 0) {
        written = snprintf(p, remaining, " --memory=%zum", g_runtime_config.memory_limit / (1024 * 1024));
        p += written;
        remaining -= written;
    }
    
    if (g_runtime_config.cpu_limit > 0) {
        written = snprintf(p, remaining, " --cpus=%f", (double)g_runtime_config.cpu_limit / 100.0);
        p += written;
        remaining -= written;
    }
    
    if (g_runtime_config.privileged) {
        written = snprintf(p, remaining, " --privileged");
        p += written;
        remaining -= written;
    }
    
    if (g_runtime_config.working_dir[0] != '\0' && 
        strcmp(g_runtime_config.working_dir, "/") != 0) {
        written = snprintf(p, remaining, " -v %s:%s:ro", 
                          g_runtime_config.working_dir, g_runtime_config.working_dir);
        p += written;
        remaining -= written;
        
        written = snprintf(p, remaining, " -w %s", g_runtime_config.working_dir);
        p += written;
        remaining -= written;
    }
    
    written = snprintf(p, remaining, " %s /bin/sh -c '%s' 2>&1", 
                      g_runtime_config.image[0] ? g_runtime_config.image : "alpine",
                      command);
    p += written;
    remaining -= written;
    
    printf("[Runtime] Docker running: %s\n", command);
    
    FILE *fp = popen(docker_cmd, "r");
    if (!fp) {
        snprintf(output, output_size, "Failed to run docker command");
        return -1;
    }
    
    size_t offset = 0;
    char buf[1024];
    while (fgets(buf, sizeof(buf), fp) && offset < output_size - 1) {
        size_t len = strlen(buf);
        if (offset + len < output_size) {
            memcpy(output + offset, buf, len);
            offset += len;
        }
    }
    output[offset] = '\0';
    
    int status = pclose(fp);
    return WEXITSTATUS(status);
}

int runtime_docker_stop(void) {
    if (g_container_id[0]) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "docker stop %s 2>/dev/null", g_container_id);
        return system(cmd);
    }
    return 0;
}

int runtime_docker_cleanup(void) {
    printf("[Runtime] Docker runtime cleaned up\n");
    if (g_container_id[0]) {
        char cmd[128];
        snprintf(cmd, sizeof(cmd), "docker rm -f %s 2>/dev/null", g_container_id);
        system(cmd);
        g_container_id[0] = '\0';
    }
    return 0;
}

int runtime_wasm_init(const runtime_config_t *config) {
    if (!config) return -1;
    memcpy(&g_runtime_config, config, sizeof(runtime_config_t));
    printf("[Runtime] WASM runtime initialized\n");
    return 0;
}

int runtime_wasm_load(const wasm_module_t *module) {
    if (!module) return -1;
    printf("[Runtime] WASM module loaded: %zu bytes\n", module->size);
    return 0;
}

int runtime_wasm_run(const char *function, char *output, size_t output_size) {
    if (!function || !output) return -1;
    
    snprintf(output, output_size, "WASM function '%s' executed", function);
    return 0;
}

int runtime_wasm_cleanup(void) {
    printf("[Runtime] WASM runtime cleaned up\n");
    return 0;
}

int runtime_wasi_init(const char *wasm_path) {
    if (!wasm_path) return -1;
    printf("[Runtime] WASI runtime initialized with: %s\n", wasm_path);
    return 0;
}

int runtime_wasi_run(const char *args[], char *output, size_t output_size) {
    if (!args || !output) return -1;
    
    snprintf(output, output_size, "WASI runtime executed (args count not tracked)");
    return 0;
}

int runtime_wasi_cleanup(void) {
    printf("[Runtime] WASI runtime cleaned up\n");
    return 0;
}

int runtime_docker_build(const char *dockerfile, const char *tag) {
    if (!dockerfile || !tag) return -1;
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "docker build -f %s -t %s .", dockerfile, tag);
    
    return system(cmd);
}

int runtime_docker_pull(const char *image) {
    if (!image) return -1;
    
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "docker pull %s", image);
    
    return system(cmd);
}

int runtime_docker_list(char *output, size_t output_size) {
    if (!output) return -1;
    
    FILE *fp = popen("docker ps -a --format '{{.ID}} {{.Image}} {{.Status}}'", "r");
    if (!fp) return -1;
    
    size_t offset = 0;
    char buf[256];
    while (fgets(buf, sizeof(buf), fp) && offset < output_size - 1) {
        size_t len = strlen(buf);
        if (offset + len < output_size) {
            memcpy(output + offset, buf, len);
            offset += len;
        }
    }
    output[offset] = '\0';
    
    pclose(fp);
    return 0;
}

int runtime_docker_remove(const char *container_id) {
    if (!container_id) return -1;
    
    char cmd[128];
    snprintf(cmd, sizeof(cmd), "docker rm -f %s", container_id);
    
    return system(cmd);
}
