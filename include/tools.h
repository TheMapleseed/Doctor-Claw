#ifndef DOCTORCLAW_TOOLS_H
#define DOCTORCLAW_TOOLS_H

#include "c23_check.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_TOOL_NAME 64
#define MAX_TOOL_DESC 512
#define MAX_TOOL_PARAMS 8192

typedef struct {
    char name[MAX_TOOL_NAME];
    char description[MAX_TOOL_DESC];
    char parameters[MAX_TOOL_PARAMS];
    bool requires_approval;
} tool_spec_t;

typedef struct {
    char name[MAX_TOOL_NAME];
    char result[16384];
    bool success;
    char error[512];
    uint64_t execution_time_ms;
} tool_result_t;

typedef struct tool_impl tool_impl_t;

typedef int (*tool_execute_fn)(const tool_impl_t *tool, const char *params, tool_result_t *result);

struct tool_impl {
    char name[MAX_TOOL_NAME];
    char description[MAX_TOOL_DESC];
    tool_execute_fn execute;
    bool requires_approval;
    void *user_data;
    tool_impl_t *next;
};

int tools_init(void);
int tools_register(const tool_impl_t *tool);
/** Set approval context for risky tools (shell, write, rm). Pass NULL to disable. */
void tools_set_approval_context(void *approval_manager, bool require_approval);
int tools_execute(const char *tool_name, const char *params, tool_result_t *result);
void tools_list(tool_spec_t **out_tools, size_t *out_count);
void tools_shutdown(void);

int tool_shell_execute(const char *params, tool_result_t *result);
int tool_file_read_execute(const char *params, tool_result_t *result);
int tool_file_write_execute(const char *params, tool_result_t *result);
int tool_browse_execute(const char *params, tool_result_t *result);
int tool_glob_execute(const char *params, tool_result_t *result);
int tool_grep_execute(const char *params, tool_result_t *result);
int tool_stat_execute(const char *params, tool_result_t *result);
int tool_list_execute(const char *params, tool_result_t *result);
int tool_mkdir_execute(const char *params, tool_result_t *result);
int tool_rm_execute(const char *params, tool_result_t *result);
int tool_cp_execute(const char *params, tool_result_t *result);
int tool_mv_execute(const char *params, tool_result_t *result);
int tool_env_execute(const char *params, tool_result_t *result);
int tool_setenv_execute(const char *params, tool_result_t *result);
int tool_exists_execute(const char *params, tool_result_t *result);
int tool_edit_execute(const char *params, tool_result_t *result);
int tool_http_request_execute(const char *params, tool_result_t *result);
int tool_web_search_execute(const char *params, tool_result_t *result);
int tool_screenshot_execute(const char *params, tool_result_t *result);
int tool_image_info_execute(const char *params, tool_result_t *result);
int tool_git_clone_execute(const char *params, tool_result_t *result);
int tool_git_pull_execute(const char *params, tool_result_t *result);
int tool_git_commit_execute(const char *params, tool_result_t *result);
int tool_cron_add_execute(const char *params, tool_result_t *result);
int tool_cron_list_execute(const char *params, tool_result_t *result);
int tool_cron_remove_execute(const char *params, tool_result_t *result);
int tool_cron_run_execute(const char *params, tool_result_t *result);
int tool_hardware_board_info_execute(const char *params, tool_result_t *result);
int tool_hardware_memory_read_execute(const char *params, tool_result_t *result);
int tool_hardware_memory_map_execute(const char *params, tool_result_t *result);
int tool_memory_store_execute(const char *params, tool_result_t *result);
int tool_memory_recall_execute(const char *params, tool_result_t *result);
int tool_memory_forget_execute(const char *params, tool_result_t *result);
int tool_schedule_execute(const char *params, tool_result_t *result);
int tool_pushover_execute(const char *params, tool_result_t *result);
int tool_delegate_execute(const char *params, tool_result_t *result);
int tool_composio_execute(const char *params, tool_result_t *result);
int tool_browser_open_execute(const char *params, tool_result_t *result);
int tool_cron_runs_execute(const char *params, tool_result_t *result);
int tool_cron_update_execute(const char *params, tool_result_t *result);
int tool_proxy_config_execute(const char *params, tool_result_t *result);
int tool_schema_execute(const char *params, tool_result_t *result);
int tool_browser_execute(const char *params, tool_result_t *result);
int tool_git_operations_execute(const char *params, tool_result_t *result);

#endif
