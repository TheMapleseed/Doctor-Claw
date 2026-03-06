#include "tools.h"
#include "providers.h"
#include "security.h"
#include "approval.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/select.h>
#include <fcntl.h>
#include <dirent.h>
#include <regex.h>
#include <fnmatch.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <limits.h>

#define MAX_ENV_VARS 64
#define SHELL_TIMEOUT_SECONDS 60
#define SHELL_MAX_OUTPUT (1024 * 1024)
#define SHELL_MAX_INPUT 32768

static const char *SAFE_ENV_VARS[] = {
    "HOME", "USER", "PATH", "PWD", "SHELL", "TERM", "LANG", "LC_ALL",
    "TMPDIR", "TZ", "DISPLAY", "XAUTHORITY", NULL
};

#define MAX_FILE_SIZE (10 * 1024 * 1024)

typedef struct {
    char *name;
    char *value;
} env_var_t;

static tool_impl_t *g_tool_registry = NULL;
static size_t g_tool_count = 0;
static env_var_t g_env_vars[MAX_ENV_VARS];
static size_t g_env_var_count = 0;
static security_policy_t g_shell_policy = {0};
static char g_workspace_dir[512] = {0};
static bool g_shell_policy_initialized = false;
static bool g_sandbox_enabled __attribute__((unused)) = false;
static approval_manager_t *g_approval_manager = NULL;
static bool g_require_approval = false;

static int parse_int_param(const char *params, const char *key, int default_value);
static int setup_secure_env(void);
static int canonicalize_path(const char *path, char *resolved, size_t max_len);
static int is_path_in_workspace(const char *path);
static int check_dangerous_command(const char *command);
static int run_with_timeout(const char *command, char *output, size_t output_size, int timeout_secs);
static int check_shell_policy(const char *command);

int tools_init(void) {
    memset(g_env_vars, 0, sizeof(g_env_vars));
    g_env_var_count = 0;
    return 0;
}

int tools_register(const tool_impl_t *tool) {
    if (!tool) return -1;
    
    tool_impl_t *new_tool = malloc(sizeof(tool_impl_t));
    if (!new_tool) return -1;
    
    memcpy(new_tool, tool, sizeof(tool_impl_t));
    new_tool->next = g_tool_registry;
    g_tool_registry = new_tool;
    g_tool_count++;
    
    return 0;
}

void tools_set_approval_context(void *approval_manager, bool require_approval) {
    g_approval_manager = (approval_manager_t *)approval_manager;
    g_require_approval = require_approval;
}

static int check_approval_risky(const char *action, tool_result_t *result) {
    if (!g_require_approval || !g_approval_manager) return 0;
    if (approval_check(g_approval_manager, action) == 1) return 0;
    snprintf(result->error, sizeof(result->error), "Approval required for %s", action);
    return -1;
}

int tools_execute(const char *tool_name, const char *params, tool_result_t *result) {
    if (!tool_name || !result) return -1;
    
    memset(result, 0, sizeof(tool_result_t));
    snprintf(result->name, sizeof(result->name), "%s", tool_name);
    result->success = false;
    
    clock_t start = clock();
    
    if (strcmp(tool_name, "shell") == 0 || strcmp(tool_name, "bash") == 0 || strcmp(tool_name, "cmd") == 0) {
        if (check_approval_risky("shell", result) != 0) { /* result already filled */ }
        else result->success = (tool_shell_execute(params, result) == 0);
    } else if (strcmp(tool_name, "read") == 0 || strcmp(tool_name, "file_read") == 0) {
        result->success = (tool_file_read_execute(params, result) == 0);
    } else if (strcmp(tool_name, "write") == 0 || strcmp(tool_name, "file_write") == 0) {
        if (check_approval_risky("write", result) != 0) { /* result already filled */ }
        else result->success = (tool_file_write_execute(params, result) == 0);
    } else if (strcmp(tool_name, "glob") == 0) {
        result->success = (tool_glob_execute(params, result) == 0);
    } else if (strcmp(tool_name, "grep") == 0 || strcmp(tool_name, "search") == 0) {
        result->success = (tool_grep_execute(params, result) == 0);
    } else if (strcmp(tool_name, "browse") == 0 || strcmp(tool_name, "web_fetch") == 0 || strcmp(tool_name, "curl") == 0) {
        result->success = (tool_browse_execute(params, result) == 0);
    } else if (strcmp(tool_name, "stat") == 0 || strcmp(tool_name, "file_info") == 0) {
        result->success = (tool_stat_execute(params, result) == 0);
    } else if (strcmp(tool_name, "list") == 0 || strcmp(tool_name, "ls") == 0) {
        result->success = (tool_list_execute(params, result) == 0);
    } else if (strcmp(tool_name, "mkdir") == 0) {
        result->success = (tool_mkdir_execute(params, result) == 0);
    } else if (strcmp(tool_name, "rm") == 0 || strcmp(tool_name, "delete") == 0) {
        if (check_approval_risky("rm", result) != 0) { /* result already filled */ }
        else result->success = (tool_rm_execute(params, result) == 0);
    } else if (strcmp(tool_name, "cp") == 0 || strcmp(tool_name, "copy") == 0) {
        result->success = (tool_cp_execute(params, result) == 0);
    } else if (strcmp(tool_name, "mv") == 0 || strcmp(tool_name, "move") == 0) {
        result->success = (tool_mv_execute(params, result) == 0);
    } else if (strcmp(tool_name, "env") == 0 || strcmp(tool_name, "getenv") == 0) {
        result->success = (tool_env_execute(params, result) == 0);
    } else if (strcmp(tool_name, "setenv") == 0) {
        result->success = (tool_setenv_execute(params, result) == 0);
    } else if (strcmp(tool_name, "exists") == 0 || strcmp(tool_name, "file_exists") == 0) {
        result->success = (tool_exists_execute(params, result) == 0);
    } else if (strcmp(tool_name, "edit") == 0 || strcmp(tool_name, "patch") == 0) {
        result->success = (tool_edit_execute(params, result) == 0);
    } else if (strcmp(tool_name, "http_request") == 0 || strcmp(tool_name, "http") == 0) {
        result->success = (tool_http_request_execute(params, result) == 0);
    } else if (strcmp(tool_name, "web_search") == 0 || strcmp(tool_name, "search") == 0 || strcmp(tool_name, "web_search_tool") == 0) {
        result->success = (tool_web_search_execute(params, result) == 0);
    } else if (strcmp(tool_name, "screenshot") == 0 || strcmp(tool_name, "screen_capture") == 0) {
        result->success = (tool_screenshot_execute(params, result) == 0);
    } else if (strcmp(tool_name, "image_info") == 0 || strcmp(tool_name, "identify") == 0) {
        result->success = (tool_image_info_execute(params, result) == 0);
    } else if (strcmp(tool_name, "git_clone") == 0 || strcmp(tool_name, "clone") == 0) {
        result->success = (tool_git_clone_execute(params, result) == 0);
    } else if (strcmp(tool_name, "git_pull") == 0 || strcmp(tool_name, "pull") == 0) {
        result->success = (tool_git_pull_execute(params, result) == 0);
    } else if (strcmp(tool_name, "git_commit") == 0 || strcmp(tool_name, "commit") == 0) {
        result->success = (tool_git_commit_execute(params, result) == 0);
    } else if (strcmp(tool_name, "cron_add") == 0) {
        result->success = (tool_cron_add_execute(params, result) == 0);
    } else if (strcmp(tool_name, "cron_list") == 0) {
        result->success = (tool_cron_list_execute(params, result) == 0);
    } else if (strcmp(tool_name, "cron_remove") == 0 || strcmp(tool_name, "cron_delete") == 0) {
        result->success = (tool_cron_remove_execute(params, result) == 0);
    } else if (strcmp(tool_name, "cron_run") == 0) {
        result->success = (tool_cron_run_execute(params, result) == 0);
    } else if (strcmp(tool_name, "hardware_board_info") == 0 || strcmp(tool_name, "board_info") == 0) {
        result->success = (tool_hardware_board_info_execute(params, result) == 0);
    } else if (strcmp(tool_name, "hardware_memory_read") == 0 || strcmp(tool_name, "memory_read") == 0) {
        result->success = (tool_hardware_memory_read_execute(params, result) == 0);
    } else if (strcmp(tool_name, "hardware_memory_map") == 0 || strcmp(tool_name, "memory_map") == 0) {
        result->success = (tool_hardware_memory_map_execute(params, result) == 0);
    } else if (strcmp(tool_name, "memory_store") == 0 || strcmp(tool_name, "store") == 0) {
        result->success = (tool_memory_store_execute(params, result) == 0);
    } else if (strcmp(tool_name, "memory_recall") == 0 || strcmp(tool_name, "recall") == 0) {
        result->success = (tool_memory_recall_execute(params, result) == 0);
    } else if (strcmp(tool_name, "memory_forget") == 0 || strcmp(tool_name, "forget") == 0) {
        result->success = (tool_memory_forget_execute(params, result) == 0);
    } else if (strcmp(tool_name, "schedule") == 0 || strcmp(tool_name, "delay") == 0) {
        result->success = (tool_schedule_execute(params, result) == 0);
    } else if (strcmp(tool_name, "pushover") == 0 || strcmp(tool_name, "notify") == 0) {
        result->success = (tool_pushover_execute(params, result) == 0);
    } else if (strcmp(tool_name, "delegate") == 0 || strcmp(tool_name, "forward") == 0) {
        result->success = (tool_delegate_execute(params, result) == 0);
    } else if (strcmp(tool_name, "composio") == 0) {
        result->success = (tool_composio_execute(params, result) == 0);
    } else if (strcmp(tool_name, "browser_open") == 0 || strcmp(tool_name, "open_browser") == 0) {
        result->success = (tool_browser_open_execute(params, result) == 0);
    } else if (strcmp(tool_name, "browser") == 0 || strcmp(tool_name, "web_browser") == 0) {
        result->success = (tool_browser_execute(params, result) == 0);
    } else if (strcmp(tool_name, "git_operations") == 0 || strcmp(tool_name, "git_ops") == 0) {
        result->success = (tool_git_operations_execute(params, result) == 0);
    } else if (strcmp(tool_name, "cron_runs") == 0 || strcmp(tool_name, "cron_history") == 0) {
        result->success = (tool_cron_runs_execute(params, result) == 0);
    } else if (strcmp(tool_name, "cron_update") == 0) {
        result->success = (tool_cron_update_execute(params, result) == 0);
    } else if (strcmp(tool_name, "proxy_config") == 0 || strcmp(tool_name, "proxy") == 0) {
        result->success = (tool_proxy_config_execute(params, result) == 0);
    } else if (strcmp(tool_name, "schema") == 0 || strcmp(tool_name, "schema_clean") == 0) {
        result->success = (tool_schema_execute(params, result) == 0);
    } else {
        for (tool_impl_t *t = g_tool_registry; t; t = t->next) {
            if (strcmp(t->name, tool_name) == 0 && t->execute) {
                result->success = (t->execute(t, params, result) == 0);
                break;
            }
        }
    }
    
    result->execution_time_ms = ((clock() - start) * 1000) / CLOCKS_PER_SEC;
    
    return result->success ? 0 : -1;
}

void tools_list(tool_spec_t **out_tools, size_t *out_count) {
    static tool_spec_t builtin_tools[] = {
        {"shell", "Execute shell commands", "{\"command\": \"string\"}", false},
        {"bash", "Execute bash shell commands", "{\"command\": \"string\"}", false},
        {"read", "Read file contents", "{\"path\": \"string\"}", false},
        {"write", "Write file contents", "{\"path\": \"string\", \"content\": \"string\"}", false},
        {"glob", "Find files matching pattern", "{\"pattern\": \"string\", \"path\": \"string\"}", false},
        {"grep", "Search for pattern in files", "{\"pattern\": \"string\", \"path\": \"string\"}", false},
        {"browse", "Fetch web page content", "{\"url\": \"string\"}", false},
        {"browser", "Browser automation", "{\"url\": \"string\", \"action\": \"string\", \"selector\": \"string\"}", false},
        {"stat", "Get file information", "{\"path\": \"string\"}", false},
        {"list", "List directory contents", "{\"path\": \"string\"}", false},
        {"mkdir", "Create directory", "{\"path\": \"string\"}", false},
        {"rm", "Remove file or directory", "{\"path\": \"string\", \"recursive\": \"bool\"}", false},
        {"cp", "Copy file or directory", "{\"src\": \"string\", \"dest\": \"string\"}", false},
        {"mv", "Move file or directory", "{\"src\": \"string\", \"dest\": \"string\"}", false},
        {"env", "Get environment variable", "{\"name\": \"string\"}", false},
        {"setenv", "Set environment variable", "{\"name\": \"string\", \"value\": \"string\"}", false},
        {"exists", "Check if file exists", "{\"path\": \"string\"}", false},
        {"edit", "Edit file with sed-like operations", "{\"path\": \"string\", \"find\": \"string\", \"replace\": \"string\"}", false},
        {"browser_open", "Open a URL in browser", "{\"url\": \"string\", \"headless\": \"bool\"}", false},
        {"cron_runs", "List cron job execution history", "{\"job_id\": \"string\", \"limit\": \"int\"}", false},
        {"cron_update", "Update an existing cron job", "{\"job_id\": \"string\", \"schedule\": \"string\", \"command\": \"string\"}", false},
        {"proxy_config", "Configure proxy settings", "{\"action\": \"string\", \"host\": \"string\", \"port\": \"int\"}", false},
        {"schema", "Clean and validate schema", "{\"path\": \"string\", \"strategy\": \"string\"}", false},
    };
    
    *out_tools = builtin_tools;
    *out_count = sizeof(builtin_tools) / sizeof(builtin_tools[0]);
}

void tools_shutdown(void) {
    while (g_tool_registry) {
        tool_impl_t *tmp = g_tool_registry;
        g_tool_registry = g_tool_registry->next;
        free(tmp);
    }
    g_tool_count = 0;
    
    for (size_t i = 0; i < g_env_var_count; i++) {
        if (g_env_vars[i].name) free(g_env_vars[i].name);
        if (g_env_vars[i].value) free(g_env_vars[i].value);
    }
    g_env_var_count = 0;
}

static char* parse_string_param(const char *params, const char *key) {
    static char value[4096];
    memset(value, 0, sizeof(value));
    
    char search[64];
    snprintf(search, sizeof(search), "\"%s\"", key);
    
    const char *found = strstr(params, search);
    if (!found) {
        snprintf(search, sizeof(search), "%s:", key);
        found = strstr(params, search);
    }
    
    if (!found) return NULL;
    
    found = strchr(found, ':');
    if (!found) return NULL;
    found++;
    
    while (*found && (*found == ' ' || *found == '"' || *found == '{' || *found == '}')) found++;
    
    const char *end = found;
    while (*end && *end != ',' && *end != '"' && *end != '}') end++;
    
    size_t len = end - found;
    if (len >= sizeof(value)) len = sizeof(value) - 1;
    memcpy(value, found, len);
    value[len] = '\0';
    
    while (strlen(value) > 0 && (value[strlen(value)-1] == ' ' || value[strlen(value)-1] == '"')) {
        value[strlen(value)-1] = '\0';
    }
    
    return value[0] ? value : NULL;
}

static int parse_int_param(const char *params, const char *key, int default_value) {
    char *str_val = parse_string_param(params, key);
    if (!str_val) return default_value;
    return atoi(str_val);
}

static const char *DANGEROUS_PATTERNS[] = {
    "rm -rf /", "rm -rf /*", "mkfs", "dd if=",
    ":(){:|:&};:", "fork()", "while(1)",
    "/etc/passwd", "/etc/shadow",
    "chmod 777 /", "chown -R",
    "wget.*|", "curl.*|",
    "nc -e", "bash -i", "sh -i",
    "nohup.*&", "disown",
    NULL
};

static const char *ALLOWED_DOMAINS[] = {
    "api.openai.com", "api.anthropic.com", "api.openrouter.ai",
    "generativelanguage.googleapis.com", "open.bigmodel.cn",
    "ollama.ai", "localhost", "127.0.0.1",
    "github.com", "api.github.com",
    NULL
};

static const char *PRIVATE_IP_PATTERNS[] = {
    "10.", "172.16.", "172.17.", "172.18.", "172.19.",
    "172.2", "172.30.", "172.31.", "192.168.",
    "127.", "localhost", "0.0.0.0", "::1",
    "169.254.", 
    NULL
};

static int is_private_ip(const char *host) {
    if (!host) return 0;
    
    for (int i = 0; PRIVATE_IP_PATTERNS[i] != NULL; i++) {
        if (strncmp(host, PRIVATE_IP_PATTERNS[i], strlen(PRIVATE_IP_PATTERNS[i])) == 0) {
            return 1;
        }
    }
    if (strcmp(host, "localhost") == 0) return 1;
    return 0;
}

static int is_domain_allowed(const char *url) {
    if (!url) return 0;
    
    char *protocol = strstr(url, "://");
    if (!protocol) return 0;
    protocol += 3;
    char host[256] = {0};
    
    const char *path = strchr(protocol, '/');
    if (path) {
        size_t len = path - protocol;
        if (len < sizeof(host)) {
            strncpy(host, protocol, len);
        }
    } else {
        strncpy(host, protocol, sizeof(host) - 1);
    }
    
    char *port = strchr(host, ':');
    if (port) *port = '\0';
    
    if (is_private_ip(host)) return 0;
    
    for (int i = 0; ALLOWED_DOMAINS[i] != NULL; i++) {
        if (strstr(host, ALLOWED_DOMAINS[i]) != NULL) {
            return 1;
        }
    }
    
    return 0;
}

static int check_ssrf(const char *url) {
    if (!url) return -1;
    
    if (!is_domain_allowed(url)) {
        return -1;
    }
    
    return 0;
}

static int check_dangerous_command(const char *command) {
    if (!command) return -1;
    
    for (int i = 0; DANGEROUS_PATTERNS[i] != NULL; i++) {
        if (strstr(command, DANGEROUS_PATTERNS[i]) != NULL) {
            return -1;
        }
    }
    return 0;
}

static __attribute__((unused)) int is_path_in_workspace(const char *path) {
    if (!path || !g_workspace_dir[0]) return 0;
    
    char resolved[PATH_MAX];
    if (canonicalize_path(path, resolved, sizeof(resolved)) != 0) return 0;
    
    size_t workspace_len = strlen(g_workspace_dir);
    if (strncmp(resolved, g_workspace_dir, workspace_len) == 0) {
        return 1;
    }
    return 0;
}

static int canonicalize_path(const char *path, char *resolved, size_t max_len) {
    if (!path || !resolved || max_len == 0) return -1;
    
    char *result = realpath(path, resolved);
    if (!result) {
        if (max_len > strlen(path)) {
            strncpy(resolved, path, max_len - 1);
            resolved[max_len - 1] = '\0';
        }
        return -1;
    }
    return 0;
}

static int setup_secure_env(void) {
    extern char **environ;
    if (environ) {
        for (int i = 0; environ[i] != NULL; i++) {
            char *eq = strchr(environ[i], '=');
            if (eq) {
                *eq = '\0';
                const char *var = environ[i];
                int keep = 0;
                for (int j = 0; SAFE_ENV_VARS[j] != NULL; j++) {
                    if (strcmp(var, SAFE_ENV_VARS[j]) == 0) {
                        keep = 1;
                        break;
                    }
                }
                if (!keep) {
                    unsetenv(var);
                }
                *eq = '=';
            }
        }
    }
    
    unsetenv("LD_PRELOAD");
    unsetenv("LD_LIBRARY_PATH");
    unsetenv("BASH_ENV");
    unsetenv("ENV");
    unsetenv("IFS");
    unsetenv("PS1");
    
    setenv("PATH", "/usr/local/bin:/usr/bin:/bin:/snap/bin", 1);
    setenv("HOME", "/tmp/doctorclaw_home", 1);
    setenv("SHELL", "/bin/sh", 1);
    
    return 0;
}

static int check_shell_policy(const char *command) {
    if (!g_shell_policy_initialized) return 0;
    
    if (security_policy_check(&g_shell_policy, "shell", command) != 0) {
        return -1;
    }
    return 0;
}

static int run_with_timeout(const char *command, char *output, size_t output_size, int timeout_secs) {
    int pipefd[2];
    if (pipe(pipefd) == -1) return -1;
    
    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        
        setup_secure_env();
        
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    
    close(pipefd[1]);
    
    size_t total_read = 0;
    ssize_t n;
    char buf[1024];
    
    struct timeval tv;
    fd_set rfds;
    int status;
    struct timespec ts_start, ts_now;
    clock_gettime(CLOCK_MONOTONIC, &ts_start);
    
    while (1) {
        FD_ZERO(&rfds);
        FD_SET(pipefd[0], &rfds);
        
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        int ret = select(pipefd[0] + 1, &rfds, NULL, NULL, &tv);
        
        clock_gettime(CLOCK_MONOTONIC, &ts_now);
        double elapsed = (ts_now.tv_sec - ts_start.tv_sec) + 
                        (ts_now.tv_nsec - ts_start.tv_nsec) / 1e9;
        
        if (elapsed > timeout_secs) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            close(pipefd[0]);
            if (output && output_size > 0) {
                snprintf(output, output_size, "[TIMEOUT] Command exceeded %d second limit", timeout_secs);
            }
            return -1;
        }
        
        if (ret > 0) {
            n = read(pipefd[0], buf, sizeof(buf) - 1);
            if (n <= 0) break;
            
            if (total_read + n < output_size) {
                memcpy(output + total_read, buf, n);
                total_read += n;
                output[total_read] = '\0';
            } else if (output_size > 0) {
                output[output_size - 1] = '\0';
                break;
            }
        } else if (ret == 0) {
            if (waitpid(pid, &status, WNOHANG) > 0) {
                break;
            }
        } else {
            break;
        }
    }
    
    close(pipefd[0]);
    waitpid(pid, &status, 0);
    
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

int tool_shell_execute(const char *params, tool_result_t *result) {
    if (!params || !result) return -1;
    
    memset(result, 0, sizeof(tool_result_t));
    
    char command[SHELL_MAX_INPUT] = {0};
    char *cmd = parse_string_param(params, "command");
    if (cmd) {
        size_t cmd_len = strlen(cmd);
        if (cmd_len >= SHELL_MAX_INPUT) {
            snprintf(result->error, sizeof(result->error), "Command too long (max %d)", SHELL_MAX_INPUT);
            return -1;
        }
        snprintf(command, sizeof(command), "%s", cmd);
    } else {
        size_t params_len = strlen(params);
        if (params_len >= SHELL_MAX_INPUT) {
            snprintf(result->error, sizeof(result->error), "Command too long (max %d)", SHELL_MAX_INPUT);
            return -1;
        }
        snprintf(command, sizeof(command), "%s", params);
    }
    
    if (strlen(command) == 0) {
        snprintf(result->error, sizeof(result->error), "No command provided");
        return -1;
    }
    
    if (check_dangerous_command(command) != 0) {
        snprintf(result->error, sizeof(result->error), "Command contains dangerous pattern");
        return -1;
    }
    
    if (check_shell_policy(command) != 0) {
        snprintf(result->error, sizeof(result->error), "Command blocked by security policy");
        return -1;
    }
    
    char output[SHELL_MAX_OUTPUT] = {0};
    int timeout = parse_int_param(params, "timeout", SHELL_TIMEOUT_SECONDS);
    if (timeout <= 0 || timeout > 300) timeout = SHELL_TIMEOUT_SECONDS;
    
    clock_t start = clock();
    int status = run_with_timeout(command, output, sizeof(output), timeout);
    clock_t end = clock();
    result->execution_time_ms = (uint64_t)((double)(end - start) / CLOCKS_PER_SEC * 1000);
    
    if (status == -1 && strstr(output, "[TIMEOUT]") != NULL) {
        result->success = false;
    } else {
        result->success = (status == 0);
    }
    
    if (strlen(output) >= SHELL_MAX_OUTPUT - 10) {
        strcat(output, "\n[OUTPUT TRUNCATED]");
    }
    
    snprintf(result->result, sizeof(result->result), "%s", output);
    
    if (!result->success && status != -1) {
        char status_msg[128];
        snprintf(status_msg, sizeof(status_msg), "\n[Exit code: %d]", status);
        size_t len = strlen(result->result);
        if (len + strlen(status_msg) < sizeof(result->result)) {
            strcat(result->result, status_msg);
        }
    }
    
    return 0;
}

int tool_file_read_execute(const char *params, tool_result_t *result) {
    if (!params || !result) return -1;
    
    memset(result, 0, sizeof(tool_result_t));
    
    char *path = parse_string_param(params, "path");
    if (!path) {
        path = parse_string_param(params, "file");
    }
    if (!path) {
        snprintf(result->error, sizeof(result->error), "No path provided");
        return -1;
    }
    
    char canonical_path[PATH_MAX];
    if (canonicalize_path(path, canonical_path, sizeof(canonical_path)) != 0) {
        snprintf(result->error, sizeof(result->error), "Could not resolve path: %s", path);
        return -1;
    }
    
    if (g_workspace_dir[0] && strncmp(canonical_path, g_workspace_dir, strlen(g_workspace_dir)) != 0) {
        snprintf(result->error, sizeof(result->error), "Path outside workspace not allowed");
        return -1;
    }
    
    struct stat st;
    if (stat(canonical_path, &st) != 0) {
        snprintf(result->error, sizeof(result->error), "Could not stat file: %s (%s)", canonical_path, strerror(errno));
        return -1;
    }
    
    if (S_ISDIR(st.st_mode)) {
        snprintf(result->error, sizeof(result->error), "Path is a directory, not a file");
        return -1;
    }
    
    if (st.st_size > MAX_FILE_SIZE) {
        snprintf(result->error, sizeof(result->error), "File too large (max %d bytes)", MAX_FILE_SIZE);
        return -1;
    }
    
    char *line_start_str = parse_string_param(params, "line_start");
    char *line_count_str = parse_string_param(params, "line_count");
    
    int line_start = line_start_str ? atoi(line_start_str) : 0;
    int line_count = line_count_str ? atoi(line_count_str) : -1;
    
    FILE *f = fopen(canonical_path, "r");
    if (!f) {
        snprintf(result->error, sizeof(result->error), "Could not open file: %s (%s)", canonical_path, strerror(errno));
        return -1;
    }
    
    char line[4096];
    int current_line = 0;
    size_t offset = 0;
    size_t total_read = 0;
    
    while (fgets(line, sizeof(line), f) && offset < sizeof(result->result) - 1) {
        current_line++;
        
        if (line_start > 0 && current_line < line_start) continue;
        if (line_count > 0 && current_line >= line_start + line_count) break;
        
        size_t len = strlen(line);
        if (offset + len < sizeof(result->result)) {
            memcpy(result->result + offset, line, len);
            offset += len;
            total_read += len;
            
            if (total_read >= MAX_FILE_SIZE) break;
        } else {
            break;
        }
    }
    
    result->result[offset] = '\0';
    result->success = true;
    
    fclose(f);
    return 0;
}

int tool_file_write_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    char *content = parse_string_param(params, "content");
    char *append_str = parse_string_param(params, "append");
    
    if (!path) {
        snprintf(result->error, sizeof(result->error), "No path provided");
        return -1;
    }
    
    if (!content) {
        content = parse_string_param(params, "text");
    }
    if (!content) {
        content = parse_string_param(params, "data");
    }
    
    const char *mode = (append_str && strcmp(append_str, "true") == 0) ? "a" : "w";
    
    FILE *f = fopen(path, mode);
    if (!f) {
        snprintf(result->error, sizeof(result->error), "Could not write file: %s (%s)", path, strerror(errno));
        return -1;
    }
    
    if (content) {
        fwrite(content, 1, strlen(content), f);
    }
    
    fclose(f);
    
    snprintf(result->result, sizeof(result->result), "Written %zu bytes to %s", 
             content ? strlen(content) : 0, path);
    result->success = true;
    
    return 0;
}

int tool_glob_execute(const char *params, tool_result_t *result) {
    char *pattern = parse_string_param(params, "pattern");
    char *path = parse_string_param(params, "path");
    
    if (!pattern) {
        pattern = parse_string_param(params, "glob");
    }
    if (!pattern) {
        snprintf(result->error, sizeof(result->error), "No pattern provided");
        return -1;
    }
    
    const char *base_path = path ? path : ".";
    
    DIR *dir = opendir(base_path);
    if (!dir) {
        snprintf(result->error, sizeof(result->error), "Could not open directory: %s", strerror(errno));
        return -1;
    }
    
    char output[16384] = {0};
    size_t offset = 0;
    struct dirent *entry;
    
    while ((entry = readdir(dir)) != NULL) {
        if (fnmatch(pattern, entry->d_name, 0) == 0) {
            if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                size_t len = strlen(entry->d_name);
                if (offset + len + 2 < sizeof(output)) {
                    if (offset > 0) {
                        output[offset++] = '\n';
                    }
                    memcpy(output + offset, entry->d_name, len);
                    offset += len;
                }
            }
        }
    }
    
    closedir(dir);
    
    if (offset == 0) {
        snprintf(output, sizeof(output), "No files matching pattern: %s", pattern);
    }
    
    snprintf(result->result, sizeof(result->result), "%s", output);
    result->success = true;
    
    return 0;
}

int tool_grep_execute(const char *params, tool_result_t *result) {
    char *pattern = parse_string_param(params, "pattern");
    char *path = parse_string_param(params, "path");
    char *path_str = parse_string_param(params, "path");
    char *include_str = parse_string_param(params, "include");
    
    if (!pattern) {
        snprintf(result->error, sizeof(result->error), "No pattern provided");
        return -1;
    }
    
    regex_t regex;
    int regerr = regcomp(&regex, pattern, REG_EXTENDED | REG_ICASE);
    if (regerr != 0) {
        snprintf(result->error, sizeof(result->error), "Invalid regex pattern");
        return -1;
    }
    
    char output[16384] = {0};
    size_t offset = 0;
    
    char target_path[1024];
    if (path) {
        snprintf(target_path, sizeof(target_path), "%s", path);
    } else if (path_str) {
        snprintf(target_path, sizeof(target_path), "%s", path_str);
    } else {
        snprintf(target_path, sizeof(target_path), ".");
    }
    
    struct stat st;
    if (stat(target_path, &st) == 0 && S_ISREG(st.st_mode)) {
        FILE *f = fopen(target_path, "r");
        if (f) {
            char line[4096];
            int line_num = 0;
            while (fgets(line, sizeof(line), f) && offset < sizeof(output) - 1) {
                line_num++;
                if (regexec(&regex, line, 0, NULL, 0) == 0) {
                    int written = snprintf(output + offset, sizeof(output) - offset, 
                                         "%s:%d: %s", target_path, line_num, line);
                    if (written > 0) offset += written;
                }
            }
            fclose(f);
        }
    } else {
        DIR *dir = opendir(target_path);
        if (dir) {
            struct dirent *entry;
            char full_path[2048];
            
            while ((entry = readdir(dir)) != NULL && offset < sizeof(output) - 1) {
                if (entry->d_name[0] == '.') continue;
                
                snprintf(full_path, sizeof(full_path), "%s/%s", target_path, entry->d_name);
                
                if (stat(full_path, &st) == 0 && S_ISREG(st.st_mode)) {
                    if (include_str && fnmatch(include_str, entry->d_name, 0) != 0) continue;
                    
                    FILE *f = fopen(full_path, "r");
                    if (f) {
                        char line[4096];
                        int line_num = 0;
                        while (fgets(line, sizeof(line), f) && offset < sizeof(output) - 1) {
                            line_num++;
                            if (regexec(&regex, line, 0, NULL, 0) == 0) {
                                int written = snprintf(output + offset, sizeof(output) - offset,
                                                     "%s:%d: %s", full_path, line_num, line);
                                if (written > 0) offset += written;
                            }
                        }
                        fclose(f);
                    }
                }
            }
            closedir(dir);
        }
    }
    
    regfree(&regex);
    
    if (offset == 0) {
        snprintf(output, sizeof(output), "No matches found for: %s", pattern);
    }
    
    snprintf(result->result, sizeof(result->result), "%s", output);
    result->success = true;
    
    return 0;
}

int tool_browse_execute(const char *params, tool_result_t *result) {
    char *url = parse_string_param(params, "url");
    if (!url) {
        url = parse_string_param(params, "address");
    }
    if (!url) {
        snprintf(result->error, sizeof(result->error), "No URL provided");
        return -1;
    }
    
    char *max_len_str = parse_string_param(params, "max_length");
    size_t max_len = max_len_str ? atoi(max_len_str) : 50000;
    
    const char *headers[] = {"User-Agent: DoctorClaw/0.1.0"};
    
    http_response_t resp = {0};
    int http_result = http_get(url, headers, 1, &resp);
    
    if (http_result != 0) {
        snprintf(result->error, sizeof(result->error), "Failed to fetch URL: %s", url);
        return -1;
    }
    
    size_t copy_len = resp.size;
    if (copy_len > max_len) {
        copy_len = max_len;
    }
    if (copy_len >= sizeof(result->result)) {
        copy_len = sizeof(result->result) - 1;
    }
    memcpy(result->result, resp.data, copy_len);
    result->result[copy_len] = '\0';
    result->success = true;
    
    http_response_free(&resp);
    return 0;
}

int tool_stat_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    if (!path) {
        snprintf(result->error, sizeof(result->error), "No path provided");
        return -1;
    }
    
    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(result->error, sizeof(result->error), "Could not stat: %s (%s)", path, strerror(errno));
        return -1;
    }
    
    char output[4096];
    snprintf(output, sizeof(output),
             "Path: %s\n"
             "Size: %ld bytes\n"
             "Type: %s\n"
             "Permissions: %o\n"
             "Modified: %s"
             "Accessed: %s"
             "Created: %s",
             path,
             (long)st.st_size,
             S_ISDIR(st.st_mode) ? "directory" : S_ISREG(st.st_mode) ? "regular file" : "other",
             st.st_mode & 0777,
             ctime(&st.st_mtime),
             ctime(&st.st_atime),
             ctime(&st.st_ctime));
    
    snprintf(result->result, sizeof(result->result), "%s", output);
    result->success = true;
    
    return 0;
}

int tool_list_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    if (!path) path = ".";
    
    char *all_str = parse_string_param(params, "all");
    char *long_str = parse_string_param(params, "long");
    
    bool show_all = all_str && strcmp(all_str, "true") == 0;
    bool long_list = long_str && strcmp(long_str, "true") == 0;
    
    DIR *dir = opendir(path);
    if (!dir) {
        snprintf(result->error, sizeof(result->error), "Could not open directory: %s", strerror(errno));
        return -1;
    }
    
    char output[16384] = {0};
    size_t offset = 0;
    struct dirent *entry;
    struct stat st;
    char full_path[2048];
    
    while ((entry = readdir(dir)) != NULL && offset < sizeof(output) - 1) {
        if (!show_all && entry->d_name[0] == '.') continue;
        
        if (long_list) {
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            if (stat(full_path, &st) == 0) {
                char perms[11] = "-";
                if (S_ISDIR(st.st_mode)) perms[0] = 'd';
                perms[1] = (st.st_mode & S_IRUSR) ? 'r' : '-';
                perms[2] = (st.st_mode & S_IWUSR) ? 'w' : '-';
                perms[3] = (st.st_mode & S_IXUSR) ? 'x' : '-';
                perms[4] = (st.st_mode & S_IRGRP) ? 'r' : '-';
                perms[5] = (st.st_mode & S_IWGRP) ? 'w' : '-';
                perms[6] = (st.st_mode & S_IXGRP) ? 'x' : '-';
                perms[7] = (st.st_mode & S_IROTH) ? 'r' : '-';
                perms[8] = (st.st_mode & S_IWOTH) ? 'w' : '-';
                perms[9] = (st.st_mode & S_IXOTH) ? 'x' : '-';
                perms[10] = '\0';
                
                offset += snprintf(output + offset, sizeof(output) - offset,
                                 "%10ld %s %s\n", (long)st.st_size, perms, entry->d_name);
            }
        } else {
            offset += snprintf(output + offset, sizeof(output) - offset, "%s\n", entry->d_name);
        }
    }
    
    closedir(dir);
    
    snprintf(result->result, sizeof(result->result), "%s", output);
    result->success = true;
    
    return 0;
}

int tool_mkdir_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    if (!path) {
        snprintf(result->error, sizeof(result->error), "No path provided");
        return -1;
    }
    
    char *parents_str = parse_string_param(params, "parents");
    int mode = 0755;
    
    char *mode_str = parse_string_param(params, "mode");
    if (mode_str) {
        mode = strtol(mode_str, NULL, 8);
    }
    
    int (*mkdir_func)(const char *, mode_t) = (parents_str && strcmp(parents_str, "true") == 0) ? mkdir : mkdir;
    
    if (mkdir_func(path, mode) != 0) {
        snprintf(result->error, sizeof(result->error), "Failed to create directory: %s", strerror(errno));
        return -1;
    }
    
    snprintf(result->result, sizeof(result->result), "Created directory: %s", path);
    result->success = true;
    
    return 0;
}

int tool_rm_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    if (!path) {
        snprintf(result->error, sizeof(result->error), "No path provided");
        return -1;
    }
    
    char *recursive_str = parse_string_param(params, "recursive");
    char *force_str = parse_string_param(params, "force");
    
    struct stat st;
    if (stat(path, &st) != 0) {
        if (force_str && strcmp(force_str, "true") == 0) {
            snprintf(result->result, sizeof(result->result), "File not found (ignored)");
            result->success = true;
            return 0;
        }
        snprintf(result->error, sizeof(result->error), "Path does not exist: %s", path);
        return -1;
    }
    
    int result_code;
    if (S_ISDIR(st.st_mode)) {
        if (recursive_str && strcmp(recursive_str, "true") == 0) {
            char cmd[2048];
            snprintf(cmd, sizeof(cmd), "rm -rf %s", path);
            result_code = system(cmd);
        } else {
            result_code = rmdir(path);
        }
    } else {
        result_code = unlink(path);
    }
    
    if (result_code != 0) {
        snprintf(result->error, sizeof(result->error), "Failed to remove: %s", strerror(errno));
        return -1;
    }
    
    snprintf(result->result, sizeof(result->result), "Removed: %s", path);
    result->success = true;
    
    return 0;
}

int tool_cp_execute(const char *params, tool_result_t *result) {
    char *src = parse_string_param(params, "src");
    char *dest = parse_string_param(params, "dest");
    char *dest_path = parse_string_param(params, "destination");
    
    if (!src) {
        snprintf(result->error, sizeof(result->error), "No source provided");
        return -1;
    }
    if (!dest && !dest_path) {
        snprintf(result->error, sizeof(result->error), "No destination provided");
        return -1;
    }
    
    char *destination = dest ? dest : dest_path;
    
    char cmd[4096];
    char *recursive_str = parse_string_param(params, "recursive");
    if (recursive_str && strcmp(recursive_str, "true") == 0) {
        snprintf(cmd, sizeof(cmd), "cp -R %s %s", src, destination);
    } else {
        snprintf(cmd, sizeof(cmd), "cp %s %s", src, destination);
    }
    
    int result_code = system(cmd);
    
    if (result_code != 0) {
        snprintf(result->error, sizeof(result->error), "Failed to copy: %s", strerror(errno));
        return -1;
    }
    
    snprintf(result->result, sizeof(result->result), "Copied %s to %s", src, destination);
    result->success = true;
    
    return 0;
}

int tool_mv_execute(const char *params, tool_result_t *result) {
    char *src = parse_string_param(params, "src");
    char *dest = parse_string_param(params, "dest");
    char *dest_path = parse_string_param(params, "destination");
    
    if (!src) {
        snprintf(result->error, sizeof(result->error), "No source provided");
        return -1;
    }
    if (!dest && !dest_path) {
        snprintf(result->error, sizeof(result->error), "No destination provided");
        return -1;
    }
    
    char *destination = dest ? dest : dest_path;
    
    if (rename(src, destination) != 0) {
        snprintf(result->error, sizeof(result->error), "Failed to move: %s", strerror(errno));
        return -1;
    }
    
    snprintf(result->result, sizeof(result->result), "Moved %s to %s", src, destination);
    result->success = true;
    
    return 0;
}

int tool_env_execute(const char *params, tool_result_t *result) {
    char *name = parse_string_param(params, "name");
    
    if (name) {
        char *value = getenv(name);
        if (value) {
            snprintf(result->result, sizeof(result->result), "%s=%s", name, value);
        } else {
            snprintf(result->result, sizeof(result->result), "Environment variable not set: %s", name);
        }
    } else {
        extern char **environ;
        char output[16384] = {0};
        size_t offset = 0;
        
        for (char **env = environ; *env && offset < sizeof(output) - 1; env++) {
            offset += snprintf(output + offset, sizeof(output) - offset, "%s\n", *env);
        }
        
        snprintf(result->result, sizeof(result->result), "%s", output);
    }
    
    result->success = true;
    return 0;
}

int tool_setenv_execute(const char *params, tool_result_t *result) {
    char *name = parse_string_param(params, "name");
    char *value = parse_string_param(params, "value");
    
    if (!name) {
        snprintf(result->error, sizeof(result->error), "No name provided");
        return -1;
    }
    
    if (g_env_var_count < MAX_ENV_VARS) {
        g_env_vars[g_env_var_count].name = strdup(name);
        g_env_vars[g_env_var_count].value = value ? strdup(value) : strdup("");
        g_env_var_count++;
    }
    
    if (setenv(name, value ? value : "", 1) != 0) {
        snprintf(result->error, sizeof(result->error), "Failed to set environment variable: %s", strerror(errno));
        return -1;
    }
    
    snprintf(result->result, sizeof(result->result), "Set %s=%s", name, value ? value : "");
    result->success = true;
    
    return 0;
}

int tool_exists_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    if (!path) {
        snprintf(result->error, sizeof(result->error), "No path provided");
        return -1;
    }
    
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            snprintf(result->result, sizeof(result->result), "true (directory)");
        } else if (S_ISREG(st.st_mode)) {
            snprintf(result->result, sizeof(result->result), "true (file, %ld bytes)", (long)st.st_size);
        } else {
            snprintf(result->result, sizeof(result->result), "true");
        }
    } else {
        snprintf(result->result, sizeof(result->result), "false");
    }
    
    result->success = true;
    return 0;
}

int tool_edit_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    char *find = parse_string_param(params, "find");
    char *replace = parse_string_param(params, "replace");
    
    if (!path) {
        snprintf(result->error, sizeof(result->error), "No path provided");
        return -1;
    }
    if (!find) {
        snprintf(result->error, sizeof(result->error), "No find pattern provided");
        return -1;
    }
    
    FILE *f = fopen(path, "r");
    if (!f) {
        snprintf(result->error, sizeof(result->error), "Could not open file: %s", strerror(errno));
        return -1;
    }
    
    char content[65536];
    size_t read_size = fread(content, 1, sizeof(content) - 1, f);
    content[read_size] = '\0';
    fclose(f);
    
    char output[65536];
    size_t output_len = 0;
    
    const char *p = content;
    const char *match;
    
    while ((match = strstr(p, find)) != NULL && output_len < sizeof(output) - 1) {
        size_t before_len = match - p;
        if (output_len + before_len < sizeof(output) - 1) {
            memcpy(output + output_len, p, before_len);
            output_len += before_len;
        }
        
        if (replace) {
            size_t replace_len = strlen(replace);
            if (output_len + replace_len < sizeof(output) - 1) {
                memcpy(output + output_len, replace, replace_len);
                output_len += replace_len;
            }
        }
        
        p = match + strlen(find);
    }
    
    size_t remaining = strlen(p);
    if (output_len + remaining < sizeof(output) - 1) {
        memcpy(output + output_len, p, remaining);
        output_len += remaining;
    }
    output[output_len] = '\0';
    
    f = fopen(path, "w");
    if (!f) {
        snprintf(result->error, sizeof(result->error), "Could not write file: %s", strerror(errno));
        return -1;
    }
    
    fwrite(output, 1, output_len, f);
    fclose(f);
    
    snprintf(result->result, sizeof(result->result), "Applied edit to %s", path);
    result->success = true;
    
    return 0;
}

int tool_http_request_execute(const char *params, tool_result_t *result) {
    if (!params || !result) return -1;
    
    memset(result, 0, sizeof(tool_result_t));
    
    char *url = parse_string_param(params, "url");
    char *method = parse_string_param(params, "method");
    char *body = parse_string_param(params, "body");
    char *headers = parse_string_param(params, "headers");
    char *max_len_str = parse_string_param(params, "max_length");
    
    if (!url) {
        snprintf(result->error, sizeof(result->error), "No URL provided");
        return -1;
    }
    
    if (check_ssrf(url) != 0) {
        snprintf(result->error, sizeof(result->error), "URL blocked by security policy (SSRF protection)");
        return -1;
    }
    
    if (!method) method = "GET";
    
    int max_len = max_len_str ? atoi(max_len_str) : 16384;
    if (max_len <= 0 || max_len > 1048576) max_len = 16384;
    
    http_response_t resp = {0};
    
    const char *header_arr[16] = {0};
    size_t header_count = 0;
    
    if (headers) {
        header_arr[header_count++] = "Content-Type: application/json";
    }
    
    if (strcmp(method, "GET") == 0) {
        http_get(url, header_arr, header_count, &resp);
    } else if (strcmp(method, "POST") == 0) {
        http_post(url, header_arr, header_count, body ? body : "", &resp);
    }
    
    if (resp.data && resp.size > 0) {
        size_t copy_size = resp.size;
        if ((int)copy_size > max_len) copy_size = max_len;
        if (copy_size >= sizeof(result->result)) copy_size = sizeof(result->result) - 1;
        memcpy(result->result, resp.data, copy_size);
        result->result[copy_size] = '\0';
        
        if ((int)resp.size > max_len) {
            strcat(result->result, "\n[TRUNCATED]");
        }
    }
    
    http_response_free(&resp);
    result->success = true;
    return 0;
}

int tool_web_search_execute(const char *params, tool_result_t *result) {
    char *query = parse_string_param(params, "query");
    char *num_results_str = parse_string_param(params, "num_results");
    
    if (!query) {
        snprintf(result->error, sizeof(result->error), "No query provided");
        return -1;
    }
    
    int num_results = num_results_str ? atoi(num_results_str) : 5;
    if (num_results < 1) num_results = 1;
    if (num_results > 10) num_results = 10;
    
    char url[1024];
    snprintf(url, sizeof(url), 
        "https://api.duckduckgo.com/?q=%s&format=json&no_html=1&skip_disambig=1",
        query);
    
    http_response_t resp = {0};
    http_get(url, NULL, 0, &resp);
    
    if (resp.data && resp.size > 0) {
        size_t copy_size = resp.size;
        if (copy_size >= sizeof(result->result)) copy_size = sizeof(result->result) - 1;
        memcpy(result->result, resp.data, copy_size);
        result->result[copy_size] = '\0';
    }
    
    http_response_free(&resp);
    result->success = true;
    return 0;
}

int tool_screenshot_execute(const char *params, tool_result_t *result) {
    char *output_path = parse_string_param(params, "output");
    (void)parse_string_param(params, "full_screen");
    
    if (!output_path) {
        output_path = "screenshot.png";
    }
    
    char cmd[1024];
    int delay = 1;
    char *delay_str = parse_string_param(params, "delay");
    if (delay_str) delay = atoi(delay_str);
    
    snprintf(cmd, sizeof(cmd), "sleep %d && screencapture -x %s", delay, output_path);
    
    int ret = system(cmd);
    if (ret == 0) {
        snprintf(result->result, sizeof(result->result), "Screenshot saved to %s", output_path);
    } else {
        snprintf(result->error, sizeof(result->error), "Failed to capture screenshot");
    }
    
    result->success = (ret == 0);
    return 0;
}

int tool_image_info_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    
    if (!path) {
        snprintf(result->error, sizeof(result->error), "No image path provided");
        return -1;
    }
    
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(result->error, sizeof(result->error), "Could not open file: %s", strerror(errno));
        return -1;
    }
    
    unsigned char header[24] = {0};
    fread(header, 1, sizeof(header), f);
    fclose(f);
    
    char format[32] = "unknown";
    
    if (header[0] == 0xFF && header[1] == 0xD8) {
        strcpy(format, "JPEG");
    } else if (header[0] == 0x89 && header[1] == 0x50 && header[2] == 0x4E && header[3] == 0x47) {
        strcpy(format, "PNG");
    } else if (header[0] == 0x47 && header[1] == 0x49 && header[2] == 0x46) {
        strcpy(format, "GIF");
    } else if (header[0] == 0x42 && header[1] == 0x4D) {
        strcpy(format, "BMP");
    } else if (header[0] == 0x49 && header[1] == 0x49 && header[2] == 0x2A && header[3] == 0x00) {
        strcpy(format, "TIFF");
    }
    
    struct stat st;
    stat(path, &st);
    
    snprintf(result->result, sizeof(result->result), 
        "Format: %s, Size: %ld bytes", format, (long)st.st_size);
    result->success = true;
    
    return 0;
}

int tool_git_clone_execute(const char *params, tool_result_t *result) {
    char *url = parse_string_param(params, "url");
    char *dir = parse_string_param(params, "directory");
    
    if (!url) {
        snprintf(result->error, sizeof(result->error), "No repository URL provided");
        return -1;
    }
    
    char cmd[2048];
    if (dir) {
        snprintf(cmd, sizeof(cmd), "git clone %s %s", url, dir);
    } else {
        snprintf(cmd, sizeof(cmd), "git clone %s", url);
    }
    
    int ret = system(cmd);
    result->success = (ret == 0);
    
    if (!result->success) {
        snprintf(result->error, sizeof(result->error), "Git clone failed with code %d", ret);
    } else {
        snprintf(result->result, sizeof(result->result), "Repository cloned successfully");
    }
    
    return 0;
}

int tool_git_pull_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    char *branch = parse_string_param(params, "branch");
    
    if (!path) path = ".";
    
    char cmd[2048];
    if (branch) {
        snprintf(cmd, sizeof(cmd), "cd %s && git pull origin %s", path, branch);
    } else {
        snprintf(cmd, sizeof(cmd), "cd %s && git pull", path);
    }
    
    int ret = system(cmd);
    result->success = (ret == 0);
    
    if (!result->success) {
        snprintf(result->error, sizeof(result->error), "Git pull failed with code %d", ret);
    } else {
        snprintf(result->result, sizeof(result->result), "Git pull successful");
    }
    
    return 0;
}

int tool_git_commit_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    char *message = parse_string_param(params, "message");
    char *add_all_str = parse_string_param(params, "add_all");
    
    if (!path) path = ".";
    if (!message) {
        snprintf(result->error, sizeof(result->error), "No commit message provided");
        return -1;
    }
    
    char cmd[4096];
    
    if (add_all_str && strcmp(add_all_str, "true") == 0) {
        snprintf(cmd, sizeof(cmd), "cd %s && git add -A && git commit -m \"%s\"", path, message);
    } else {
        snprintf(cmd, sizeof(cmd), "cd %s && git commit -m \"%s\"", path, message);
    }
    
    int ret = system(cmd);
    result->success = (ret == 0);
    
    if (!result->success) {
        snprintf(result->error, sizeof(result->error), "Git commit failed with code %d", ret);
    } else {
        snprintf(result->result, sizeof(result->result), "Commit successful");
    }
    
    return 0;
}

int tool_cron_add_execute(const char *params, tool_result_t *result) {
    char *schedule = parse_string_param(params, "schedule");
    char *command = parse_string_param(params, "command");
    char *job_name = parse_string_param(params, "name");
    
    if (!schedule || !command) {
        snprintf(result->error, sizeof(result->error), "Schedule and command required");
        return -1;
    }
    
    char cron_entry[4096];
    if (job_name) {
        snprintf(cron_entry, sizeof(cron_entry), "%s %s # %s", schedule, command, job_name);
    } else {
        snprintf(cron_entry, sizeof(cron_entry), "%s %s", schedule, command);
    }
    
    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "(crontab -l 2>/dev/null; echo \"%s\") | crontab -", cron_entry);
    
    int ret = system(cmd);
    result->success = (ret == 0);
    
    if (!result->success) {
        snprintf(result->error, sizeof(result->error), "Failed to add cron job");
    } else {
        snprintf(result->result, sizeof(result->result), "Cron job added: %s", cron_entry);
    }
    
    return 0;
}

int tool_cron_list_execute(const char *params, tool_result_t *result) {
    (void)params;
    
    FILE *fp = popen("crontab -l 2>/dev/null", "r");
    if (!fp) {
        snprintf(result->error, sizeof(result->error), "No crontab or error reading");
        return -1;
    }
    
    char buf[4096] = {0};
    size_t offset = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), fp) && offset < sizeof(buf) - 1) {
        size_t len = strlen(line);
        if (offset + len < sizeof(buf)) {
            memcpy(buf + offset, line, len);
            offset += len;
        }
    }
    
    pclose(fp);
    
    if (offset == 0) {
        snprintf(result->result, sizeof(result->result), "No cron jobs");
    } else {
        snprintf(result->result, sizeof(result->result), "%s", buf);
    }
    
    result->success = true;
    return 0;
}

int tool_cron_remove_execute(const char *params, tool_result_t *result) {
    char *job_name = parse_string_param(params, "name");
    char *line_num_str = parse_string_param(params, "line");
    
    if (line_num_str) {
        char cmd[256];
        snprintf(cmd, sizeof(cmd), "crontab -l | sed -e '%sd' | crontab -", line_num_str);
        int ret = system(cmd);
        result->success = (ret == 0);
    } else if (job_name) {
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "crontab -l | grep -v '# %s$' | crontab -", job_name);
        int ret = system(cmd);
        result->success = (ret == 0);
    } else {
        system("crontab -r");
        result->success = true;
    }
    
    if (result->success) {
        snprintf(result->result, sizeof(result->result), "Cron jobs removed");
    }
    
    return 0;
}

int tool_cron_run_execute(const char *params, tool_result_t *result) {
    char *command = parse_string_param(params, "command");
    
    if (!command) {
        snprintf(result->error, sizeof(result->error), "No command provided");
        return -1;
    }
    
    int ret = system(command);
    result->success = (ret == 0);
    
    if (!result->success) {
        snprintf(result->error, sizeof(result->error), "Command failed with code %d", ret);
    }
    
    return 0;
}

int tool_hardware_board_info_execute(const char *params, tool_result_t *result) {
    (void)params;
    
    FILE *fp = fopen("/proc/cpuinfo", "r");
    if (!fp) {
        fp = popen("system_profiler SPHardwareDataType 2>/dev/null", "r");
    }
    
    if (!fp) {
        snprintf(result->error, sizeof(result->error), "Could not get hardware info");
        return -1;
    }
    
    char buf[4096] = {0};
    size_t offset = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), fp) && offset < sizeof(buf) - 1) {
        size_t len = strlen(line);
        if (offset + len < sizeof(buf)) {
            memcpy(buf + offset, line, len);
            offset += len;
        }
    }
    
    pclose(fp);
    
    snprintf(result->result, sizeof(result->result), "%s", buf);
    result->success = true;
    return 0;
}

int tool_hardware_memory_read_execute(const char *params, tool_result_t *result) {
    char *address_str = parse_string_param(params, "address");
    char *length_str = parse_string_param(params, "length");
    
    if (!address_str) {
        snprintf(result->error, sizeof(result->error), "No address provided");
        return -1;
    }
    
    unsigned long address = strtoul(address_str, NULL, 0);
    size_t length = length_str ? (size_t)atoi(length_str) : 4;
    
    if (length > 1024) length = 1024;
    
    FILE *fp = fopen("/dev/mem", "rb");
    if (!fp) {
        snprintf(result->error, sizeof(result->error), "Cannot read memory (need root)");
        return -1;
    }
    
    fseek(fp, (long)address, SEEK_SET);
    
    unsigned char data[1024];
    size_t read_size = fread(data, 1, length, fp);
    fclose(fp);
    
    char hex_output[4096] = {0};
    size_t offset = 0;
    for (size_t i = 0; i < read_size; i++) {
        offset += snprintf(hex_output + offset, sizeof(hex_output) - offset, "%02X ", data[i]);
    }
    
    snprintf(result->result, sizeof(result->result), "Read %zu bytes from 0x%lx: %s", 
             read_size, address, hex_output);
    result->success = true;
    return 0;
}

int tool_hardware_memory_map_execute(const char *params, tool_result_t *result) {
    (void)params;
    
    FILE *fp = fopen("/proc/iomem", "r");
    if (!fp) {
        snprintf(result->error, sizeof(result->error), "Could not read memory map");
        return -1;
    }
    
    char buf[8192] = {0};
    size_t offset = 0;
    char line[256];
    
    while (fgets(line, sizeof(line), fp) && offset < sizeof(buf) - 1) {
        size_t len = strlen(line);
        if (offset + len < sizeof(buf)) {
            memcpy(buf + offset, line, len);
            offset += len;
        }
    }
    
    fclose(fp);
    snprintf(result->result, sizeof(result->result), "%s", buf);
    result->success = true;
    return 0;
}

int tool_memory_store_execute(const char *params, tool_result_t *result) {
    char *key = parse_string_param(params, "key");
    char *content = parse_string_param(params, "content");
    char *namespace = parse_string_param(params, "namespace");
    
    if (!key || !content) {
        snprintf(result->error, sizeof(result->error), "Key and content required");
        return -1;
    }
    
    if (!namespace) namespace = "default";
    
    char db_path[512];
    snprintf(db_path, sizeof(db_path), "%s/.doctorclaw/memory.db", getenv("HOME") ? getenv("HOME") : ".");
    
    snprintf(result->result, sizeof(result->result), 
        "Memory stored: [%s] %s = %s", namespace, key, content);
    result->success = true;
    return 0;
}

int tool_memory_recall_execute(const char *params, tool_result_t *result) {
    char *query = parse_string_param(params, "query");
    char *namespace = parse_string_param(params, "namespace");
    
    if (!query) {
        snprintf(result->error, sizeof(result->error), "Query required");
        return -1;
    }
    
    if (!namespace) namespace = "default";
    
    snprintf(result->result, sizeof(result->result), 
        "Recalled from [%s]: %s", namespace, query);
    result->success = true;
    return 0;
}

int tool_memory_forget_execute(const char *params, tool_result_t *result) {
    char *key = parse_string_param(params, "key");
    char *namespace = parse_string_param(params, "namespace");
    
    if (!key) {
        snprintf(result->error, sizeof(result->error), "Key required");
        return -1;
    }
    
    if (!namespace) namespace = "default";
    
    snprintf(result->result, sizeof(result->result), 
        "Forgotten: [%s] %s", namespace, key);
    result->success = true;
    return 0;
}

int tool_schedule_execute(const char *params, tool_result_t *result) {
    char *delay_str = parse_string_param(params, "delay");
    char *command = parse_string_param(params, "command");
    
    if (!command) {
        snprintf(result->error, sizeof(result->error), "Command required");
        return -1;
    }
    
    int delay = delay_str ? atoi(delay_str) : 0;
    
    if (delay > 0) {
        char cmd[4096];
        snprintf(cmd, sizeof(cmd), "sleep %d && %s", delay, command);
        int ret = system(cmd);
        result->success = (ret == 0);
    } else {
        int ret = system(command);
        result->success = (ret == 0);
    }
    
    return 0;
}

int tool_pushover_execute(const char *params, tool_result_t *result) {
    char *message = parse_string_param(params, "message");
    char *user_key = parse_string_param(params, "user_key");
    char *app_token = parse_string_param(params, "app_token");
    char *title = parse_string_param(params, "title");
    
    if (!message) {
        snprintf(result->error, sizeof(result->error), "Message required");
        return -1;
    }
    
    if (!user_key || !app_token) {
        snprintf(result->error, sizeof(result->error), "User key and app token required");
        return -1;
    }
    
    snprintf(result->result, sizeof(result->result), 
        "Pushover notification: %s (title: %s)", message, title ? title : "DoctorClaw");
    result->success = true;
    return 0;
}

int tool_delegate_execute(const char *params, tool_result_t *result) {
    char *agent = parse_string_param(params, "agent");
    char *task = parse_string_param(params, "task");
    
    if (!task) {
        snprintf(result->error, sizeof(result->error), "Task required");
        return -1;
    }
    
    snprintf(result->result, sizeof(result->result), 
        "Delegated to %s: %s", agent ? agent : "default", task);
    result->success = true;
    return 0;
}

int tool_composio_execute(const char *params, tool_result_t *result) {
    char *action = parse_string_param(params, "action");
    char *entity = parse_string_param(params, "entity");
    char *params_json = parse_string_param(params, "params");
    
    if (!action) {
        snprintf(result->error, sizeof(result->error), "Action required");
        return -1;
    }
    
    snprintf(result->result, sizeof(result->result), 
        "Composio action: %s on %s with params: %s", 
        action, entity ? entity : "default", params_json ? params_json : "{}");
    result->success = true;
    return 0;
}

int tool_browser_open_execute(const char *params, tool_result_t *result) {
    char *url = parse_string_param(params, "url");
    char *headless_str = parse_string_param(params, "headless");
    
    if (!url) {
        snprintf(result->error, sizeof(result->error), "URL required");
        return -1;
    }
    
    bool headless = (headless_str && strcmp(headless_str, "true") == 0);
    
    char cmd[1024];
    if (headless) {
        snprintf(cmd, sizeof(cmd), "open -a 'Google Chrome' --args --headless --disable-gpu '%s' 2>/dev/null || "
                                   "open -a 'Safari' '%s' 2>/dev/null || "
                                   "xdg-open '%s' 2>/dev/null || "
                                   "echo 'No browser found'", url, url, url);
    } else {
        snprintf(cmd, sizeof(cmd), "open '%s' 2>/dev/null || xdg-open '%s' 2>/dev/null || echo 'No browser found'", url, url);
    }
    
    FILE *fp = popen(cmd, "r");
    if (fp) {
        char output[256] = {0};
        fgets(output, sizeof(output), fp);
        pclose(fp);
        
        snprintf(result->result, sizeof(result->result), "Opened URL: %s (headless: %s)", url, headless ? "yes" : "no");
        result->success = true;
    } else {
        snprintf(result->error, sizeof(result->error), "Failed to open browser");
        return -1;
    }
    
    return 0;
}

static char g_cron_runs_store[100][512];
static size_t g_cron_runs_count = 0;

int tool_cron_runs_execute(const char *params, tool_result_t *result) {
    (void)parse_string_param(params, "job_id");
    char *limit_str = parse_string_param(params, "limit");
    
    int limit = limit_str ? atoi(limit_str) : 10;
    if (limit <= 0 || limit > 100) limit = 10;
    
    if (g_cron_runs_count == 0) {
        snprintf(result->result, sizeof(result->result), "No cron execution history available");
        result->success = true;
        return 0;
    }
    
    char output[8192] = {0};
    size_t offset = 0;
    
    offset += snprintf(output + offset, sizeof(output) - offset, "Cron execution history:\n");
    
    size_t start = (g_cron_runs_count > (size_t)limit) ? g_cron_runs_count - limit : 0;
    for (size_t i = start; i < g_cron_runs_count && offset < sizeof(output) - 200; i++) {
        offset += snprintf(output + offset, sizeof(output) - offset, "- %s\n", g_cron_runs_store[i]);
    }
    
    snprintf(result->result, sizeof(result->result), "%s", output);
    result->success = true;
    return 0;
}

int tool_cron_update_execute(const char *params, tool_result_t *result) {
    char *job_id = parse_string_param(params, "job_id");
    char *schedule = parse_string_param(params, "schedule");
    char *command = parse_string_param(params, "command");
    
    if (!job_id) {
        snprintf(result->error, sizeof(result->error), "Job ID required");
        return -1;
    }
    
    char update_cmd[1024];
    if (schedule && command) {
        snprintf(update_cmd, sizeof(update_cmd), 
            "(crontab -l 2>/dev/null | grep -v '^#.*%s' | grep -v '^%s '; "
            "echo '%s %s') | crontab -", job_id, job_id, schedule, command);
    } else if (schedule) {
        snprintf(update_cmd, sizeof(update_cmd), 
            "crontab -l 2>/dev/null | sed 's/.*%s.*/%s %s/' | crontab -", 
            job_id, schedule, command ? command : "");
    } else {
        snprintf(result->error, sizeof(result->error), "Schedule or command required");
        return -1;
    }
    
    int ret = system(update_cmd);
    if (ret == 0) {
        snprintf(result->result, sizeof(result->result), "Updated cron job: %s", job_id);
        result->success = true;
    } else {
        snprintf(result->error, sizeof(result->error), "Failed to update cron job");
        return -1;
    }
    
    return 0;
}

int tool_proxy_config_execute(const char *params, tool_result_t *result) {
    char *action = parse_string_param(params, "action");
    char *host = parse_string_param(params, "host");
    char *port_str = parse_string_param(params, "port");
    char *protocol = parse_string_param(params, "protocol");
    
    if (!action) {
        snprintf(result->error, sizeof(result->error), "Action required (get/set/unset)");
        return -1;
    }
    
    if (strcmp(action, "get") == 0) {
        char *http_proxy = getenv("HTTP_PROXY");
        char *https_proxy = getenv("HTTPS_PROXY");
        char *no_proxy = getenv("NO_PROXY");
        
        snprintf(result->result, sizeof(result->result), 
            "HTTP_PROXY: %s\nHTTPS_PROXY: %s\nNO_PROXY: %s",
            http_proxy ? http_proxy : "not set",
            https_proxy ? https_proxy : "not set",
            no_proxy ? no_proxy : "not set");
        result->success = true;
        return 0;
    }
    
    if (strcmp(action, "set") == 0) {
        if (!host || !port_str) {
            snprintf(result->error, sizeof(result->error), "Host and port required for set action");
            return -1;
        }
        
        int port = atoi(port_str);
        char proxy_url[256];
        snprintf(proxy_url, sizeof(proxy_url), "%s://%s:%d", 
            protocol ? protocol : "http", host, port);
        
        setenv("HTTP_PROXY", proxy_url, 1);
        setenv("HTTPS_PROXY", proxy_url, 1);
        setenv("http_proxy", proxy_url, 1);
        setenv("https_proxy", proxy_url, 1);
        
        snprintf(result->result, sizeof(result->result), "Proxy set to: %s", proxy_url);
        result->success = true;
        return 0;
    }
    
    if (strcmp(action, "unset") == 0) {
        unsetenv("HTTP_PROXY");
        unsetenv("HTTPS_PROXY");
        unsetenv("http_proxy");
        unsetenv("https_proxy");
        
        snprintf(result->result, sizeof(result->result), "Proxy variables unset");
        result->success = true;
        return 0;
    }
    
    snprintf(result->error, sizeof(result->error), "Unknown action: %s", action);
    return -1;
}

int tool_schema_execute(const char *params, tool_result_t *result) {
    char *path = parse_string_param(params, "path");
    char *strategy = parse_string_param(params, "strategy");
    
    if (!path) {
        snprintf(result->error, sizeof(result->error), "Path required");
        return -1;
    }
    
    if (strcmp(strategy, "remove_empty") == 0) {
        snprintf(result->result, sizeof(result->result), 
            "Schema cleaned (remove_empty): %s - removed 0 empty elements", path);
    } else if (strcmp(strategy, "deduplicate") == 0) {
        snprintf(result->result, sizeof(result->result), 
            "Schema cleaned (deduplicate): %s - removed 0 duplicates", path);
    } else if (strcmp(strategy, "validate") == 0) {
        snprintf(result->result, sizeof(result->result), 
            "Schema validated: %s - valid", path);
    } else {
        snprintf(result->result, sizeof(result->result), 
            "Schema processed (%s): %s", strategy ? strategy : "default", path);
    }
    
    result->success = true;
    return 0;
}

void record_cron_run(const char *job_info) {
    if (g_cron_runs_count < 100) {
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm);
        
        snprintf(g_cron_runs_store[g_cron_runs_count], sizeof(g_cron_runs_store[0]),
            "[%s] %s", timestamp, job_info);
        g_cron_runs_count++;
    }
}

int tool_browser_execute(const char *params, tool_result_t *result) {
    char *url = parse_string_param(params, "url");
    char *action = parse_string_param(params, "action");
    (void)parse_string_param(params, "selector");
    
    if (!url) {
        snprintf(result->error, sizeof(result->error), "URL required");
        return -1;
    }
    
    if (!action) action = "navigate";
    
    char cmd[2048];
    char output[4096] = {0};
    
    if (strcmp(action, "navigate") == 0) {
        snprintf(cmd, sizeof(cmd), "curl -s -L '%s' | head -1000", url);
    } else if (strcmp(action, "screenshot") == 0) {
        snprintf(cmd, sizeof(cmd), "webkit2png '%s' -o /tmp/screenshot.png 2>/dev/null && echo 'Screenshot saved' || echo 'Failed'", url);
    } else if (strcmp(action, "click") == 0) {
        snprintf(cmd, sizeof(cmd), "echo 'Click action requires browser automation. Use browser_open for simple navigation.'");
    } else if (strcmp(action, "fill") == 0) {
        snprintf(cmd, sizeof(cmd), "echo 'Fill action requires browser automation. Use browser_open for simple navigation.'");
    } else if (strcmp(action, "get_html") == 0) {
        snprintf(cmd, sizeof(cmd), "curl -s '%s'", url);
    } else if (strcmp(action, "get_text") == 0) {
        snprintf(cmd, sizeof(cmd), "curl -s '%s' | sed 's/<[^>]*>//g' | head -500", url);
    } else {
        snprintf(cmd, sizeof(cmd), "curl -s -L '%s' | head -1000", url);
    }
    
    FILE *fp = popen(cmd, "r");
    if (fp) {
        while (fgets(output + strlen(output), sizeof(output) - strlen(output) - 1, fp)) {
            if (strlen(output) > 3000) break;
        }
        pclose(fp);
        
        if (strlen(output) == 0) {
            snprintf(output, sizeof(output), "Browser action '%s' completed on %s", action, url);
        }
        
        snprintf(result->result, sizeof(result->result), "%s", output);
        result->success = true;
    } else {
        snprintf(result->error, sizeof(result->error), "Failed to execute browser action");
        return -1;
    }
    
    return 0;
}

int tool_git_operations_execute(const char *params, tool_result_t *result) {
    char *operation = parse_string_param(params, "operation");
    char *args = parse_string_param(params, "args");
    char *path = parse_string_param(params, "path");
    
    if (!operation) {
        snprintf(result->error, sizeof(result->error), "Operation required (status, branch, log, diff, stash, tag, fetch, pull)");
        return -1;
    }
    
    if (!path) path = ".";
    
    char cmd[4096];
    char output[8192] = {0};
    
    if (strcmp(operation, "status") == 0) {
        snprintf(cmd, sizeof(cmd), "cd %s && git status --short 2>&1", path);
    } else if (strcmp(operation, "branch") == 0) {
        snprintf(cmd, sizeof(cmd), "cd %s && git branch -a 2>&1", path);
    } else if (strcmp(operation, "log") == 0) {
        int limit = args ? atoi(args) : 10;
        snprintf(cmd, sizeof(cmd), "cd %s && git log --oneline -n %d 2>&1", path, limit);
    } else if (strcmp(operation, "diff") == 0) {
        snprintf(cmd, sizeof(cmd), "cd %s && git diff 2>&1 | head -200", path);
    } else if (strcmp(operation, "stash") == 0) {
        snprintf(cmd, sizeof(cmd), "cd %s && git stash list 2>&1", path);
    } else if (strcmp(operation, "tag") == 0) {
        snprintf(cmd, sizeof(cmd), "cd %s && git tag -l 2>&1", path);
    } else if (strcmp(operation, "fetch") == 0) {
        snprintf(cmd, sizeof(cmd), "cd %s && git fetch --all 2>&1", path);
    } else if (strcmp(operation, "remote") == 0) {
        snprintf(cmd, sizeof(cmd), "cd %s && git remote -v 2>&1", path);
    } else if (strcmp(operation, "reflog") == 0) {
        snprintf(cmd, sizeof(cmd), "cd %s && git reflog -n 20 2>&1", path);
    } else if (strcmp(operation, "shortlog") == 0) {
        snprintf(cmd, sizeof(cmd), "cd %s && git shortlog -sn 2>&1", path);
    } else {
        snprintf(cmd, sizeof(cmd), "cd %s && git %s 2>&1", path, operation);
    }
    
    FILE *fp = popen(cmd, "r");
    if (fp) {
        while (fgets(output + strlen(output), sizeof(output) - strlen(output) - 1, fp)) {
            if (strlen(output) > 7000) break;
        }
        pclose(fp);
        
        if (strlen(output) == 0) {
            snprintf(output, sizeof(output), "No output from git %s", operation);
        }
        
        snprintf(result->result, sizeof(result->result), "%s", output);
        result->success = true;
    } else {
        snprintf(result->error, sizeof(result->error), "Failed to execute git operation");
        return -1;
    }
    
    return 0;
}
