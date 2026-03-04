#include "service.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

static const char *state_names[] = {
    "unknown",
    "stopped",
    "starting",
    "running",
    "stopping",
    "failed"
};

int service_manager_init(service_manager_t *mgr) {
    if (!mgr) return -1;
    memset(mgr, 0, sizeof(service_manager_t));
    return 0;
}

int service_register(service_manager_t *mgr, const char *name, const char *executable, const char *args) {
    if (!mgr || !name || !executable) return -1;
    if (mgr->service_count >= 32) return -1;
    
    service_t *svc = &mgr->services[mgr->service_count];
    snprintf(svc->name, sizeof(svc->name), "%s", name);
    snprintf(svc->executable, sizeof(svc->executable), "%s", executable);
    if (args) {
        snprintf(svc->args, sizeof(svc->args), "%s", args);
    }
    svc->enabled = false;
    svc->state = SERVICE_STATE_STOPPED;
    svc->pid = 0;
    svc->started_at = 0;
    svc->last_exit = 0;
    svc->exit_code = 0;
    
    mgr->service_count++;
    return 0;
}

int service_unregister(service_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    for (size_t i = 0; i < mgr->service_count; i++) {
        if (strcmp(mgr->services[i].name, name) == 0) {
            if (mgr->services[i].state == SERVICE_STATE_RUNNING) {
                kill(mgr->services[i].pid, SIGTERM);
            }
            for (size_t j = i; j < mgr->service_count - 1; j++) {
                mgr->services[j] = mgr->services[j + 1];
            }
            mgr->service_count--;
            return 0;
        }
    }
    return -1;
}

int service_start(service_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    service_t *svc = NULL;
    for (size_t i = 0; i < mgr->service_count; i++) {
        if (strcmp(mgr->services[i].name, name) == 0) {
            svc = &mgr->services[i];
            break;
        }
    }
    
    if (!svc) return -1;
    if (svc->state == SERVICE_STATE_RUNNING) return 0;
    
    svc->state = SERVICE_STATE_STARTING;
    
    pid_t pid = fork();
    if (pid < 0) {
        svc->state = SERVICE_STATE_FAILED;
        return -1;
    }
    
    if (pid == 0) {
        if (svc->args[0]) {
            execl(svc->executable, svc->executable, svc->args, NULL);
        } else {
            execl(svc->executable, svc->executable, NULL);
        }
        exit(1);
    }
    
    svc->pid = pid;
    svc->state = SERVICE_STATE_RUNNING;
    svc->started_at = (uint64_t)time(NULL);
    
    return 0;
}

int service_stop(service_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    for (size_t i = 0; i < mgr->service_count; i++) {
        if (strcmp(mgr->services[i].name, name) == 0) {
            service_t *svc = &mgr->services[i];
            if (svc->state != SERVICE_STATE_RUNNING) return 0;
            
            svc->state = SERVICE_STATE_STOPPING;
            kill(svc->pid, SIGTERM);
            
            int status;
            waitpid(svc->pid, &status, 0);
            
            svc->state = SERVICE_STATE_STOPPED;
            svc->last_exit = (uint64_t)time(NULL);
            svc->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            svc->pid = 0;
            
            return 0;
        }
    }
    return -1;
}

int service_restart(service_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    service_stop(mgr, name);
    return service_start(mgr, name);
}

int service_enable(service_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    for (size_t i = 0; i < mgr->service_count; i++) {
        if (strcmp(mgr->services[i].name, name) == 0) {
            mgr->services[i].enabled = true;
            return 0;
        }
    }
    return -1;
}

int service_disable(service_manager_t *mgr, const char *name) {
    if (!mgr || !name) return -1;
    
    for (size_t i = 0; i < mgr->service_count; i++) {
        if (strcmp(mgr->services[i].name, name) == 0) {
            mgr->services[i].enabled = false;
            return 0;
        }
    }
    return -1;
}

int service_status(service_manager_t *mgr, const char *name, service_t *out_service) {
    if (!mgr || !name || !out_service) return -1;
    
    for (size_t i = 0; i < mgr->service_count; i++) {
        if (strcmp(mgr->services[i].name, name) == 0) {
            *out_service = mgr->services[i];
            return 0;
        }
    }
    return -1;
}

int service_list(service_manager_t *mgr, service_t **out_services, size_t *out_count) {
    if (!mgr || !out_services || !out_count) return -1;
    *out_services = mgr->services;
    *out_count = mgr->service_count;
    return 0;
}

int service_manager_save(service_manager_t *mgr, const char *path) {
    if (!mgr || !path) return -1;
    
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    
    fprintf(f, "# DoctorClaw Services\n\n");
    
    for (size_t i = 0; i < mgr->service_count; i++) {
        service_t *s = &mgr->services[i];
        fprintf(f, "[service]\n");
        fprintf(f, "name = %s\n", s->name);
        fprintf(f, "executable = %s\n", s->executable);
        if (s->args[0]) {
            fprintf(f, "args = %s\n", s->args);
        }
        fprintf(f, "enabled = %s\n", s->enabled ? "true" : "false");
        fprintf(f, "\n");
    }
    
    fclose(f);
    return 0;
}

int service_manager_load(service_manager_t *mgr, const char *path) {
    if (!mgr || !path) return -1;
    
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    
    char line[1024];
    char current_name[MAX_SERVICE_NAME] = {0};
    char current_exec[MAX_SERVICE_PATH] = {0};
    char current_args[1024] = {0};
    bool current_enabled = false;
    
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '[') continue;
        
        char key[128], val[512];
        if (sscanf(line, "%127[^=] = %511[^\n]", key, val) == 2) {
            char *k = key;
            while (*k == ' ') k++;
            
            if (strcmp(k, "name") == 0) {
                snprintf(current_name, sizeof(current_name), "%s", val);
            } else if (strcmp(k, "executable") == 0) {
                snprintf(current_exec, sizeof(current_exec), "%s", val);
            } else if (strcmp(k, "args") == 0) {
                snprintf(current_args, sizeof(current_args), "%s", val);
            } else if (strcmp(k, "enabled") == 0) {
                current_enabled = (strcmp(val, "true") == 0);
            }
        }
    }
    
    if (current_name[0] && current_exec[0]) {
        service_register(mgr, current_name, current_exec, current_args[0] ? current_args : NULL);
        if (current_enabled) {
            service_enable(mgr, current_name);
        }
    }
    
    fclose(f);
    return 0;
}

void service_manager_free(service_manager_t *mgr) {
    if (!mgr) return;
    
    for (size_t i = 0; i < mgr->service_count; i++) {
        if (mgr->services[i].state == SERVICE_STATE_RUNNING) {
            kill(mgr->services[i].pid, SIGTERM);
        }
    }
    
    memset(mgr, 0, sizeof(service_manager_t));
}

const char *service_state_name(service_state_t state) {
    if (state >= 0 && state <= SERVICE_STATE_FAILED) {
        return state_names[state];
    }
    return "unknown";
}
